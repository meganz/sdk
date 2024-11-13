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

#include "megacli.h"

#include "mega.h"
#include "mega/arguments.h"
#include "mega/filesystem.h"
#include "mega/gfx.h"
#include "mega/pwm_file_parser.h"
#include "mega/testhooks.h"
#include "mega/user_attribute.h"

#include <bitset>
#include <charconv>
#include <chrono>
#include <exception>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#if defined(_WIN32) && defined(_DEBUG)
// so we can delete a secret internal CrytpoPP singleton
#include <cryptopp\osrng.h>
#endif

#define USE_VARARGS
#define PREFER_STDARG

#ifndef NO_READLINE
#include <signal.h>
#include <readline/readline.h>
#include <readline/history.h>
#endif

#if (__cplusplus >= 201703L)
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

#ifdef ENABLE_ISOLATED_GFX
#include "mega/gfx/isolatedprocess.h"
#endif

#ifdef WIN32
#include <winioctl.h>
#endif

namespace ac = ::mega::autocomplete;

#include <iomanip>

// FUSE
#include <mega/fuse/common/mount_info.h>
#include <mega/fuse/common/mount_result.h>
#include <mega/fuse/common/normalized_path.h>
#include <mega/fuse/common/service_flags.h>

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

int gNextClientTag = 1;
std::map<int, std::function<void(Node*)>> gOnPutNodeTag;

bool gVerboseMode = false;

// new account signup e-mail address and name
static string signupemail, signupname;

// signup code being confirmed
static string signupcode;

// signup password challenge and encrypted master key
static byte signuppwchallenge[SymmCipher::KEYLENGTH];

// password recovery e-mail address and code being confirmed
static string recoveryemail, recoverycode;

// password recovery code requires MK or not
static bool hasMasterKey;

// master key for password recovery
static byte masterkey[SymmCipher::KEYLENGTH];

// change email link to be confirmed
static string changeemail, changecode;

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

//Ephemeral account plus plus
std::string ephemeralFirstname;
std::string ephemeralLastName;

// external drive id, used for name filtering
static string allExtDrives = "*";
string b64driveid;

void uploadLocalPath(nodetype_t type, std::string name, const LocalPath& localname, Node* parent, const std::string& targetuser,
    TransferDbCommitter& committer, int& total, bool recursive, VersioningOption vo,
    std::function<std::function<void()>(LocalPath)> onCompletedGenerator, bool noRetries, bool allowDuplicateVersions);


static std::string USAGE = R"(
Mega command line
Usage:
  megacli [OPTION...]

  -h                   Show help
  -v                   Verbose
  -c=arg               Client type. default|vpn|password_manager (default: default))"
#if defined(ENABLE_ISOLATED_GFX)
R"(
  -e=arg               Use the isolated gfx processor. This gives executable binary path
  -n=arg               Endpoint name (default: mega_gfxworker_megacli)
)"
#endif
;
struct Config
{
    std::string executable;

    std::string endpointName;

    std::string clientType;

    static Config fromArguments(const Arguments& arguments);
};

Config Config::fromArguments(const Arguments& arguments)
{
    Config config;

#if defined(ENABLE_ISOLATED_GFX)
    // executable
    config.executable = arguments.getValue("-e", "");

    FSACCESS_CLASS fsAccess;
    if (!config.executable.empty() && !fsAccess.fileExistsAt(LocalPath::fromAbsolutePath(config.executable)))
    {
        throw std::runtime_error("Couldn't find Executable: " + config.executable);
    }

    // endpoint name
    config.endpointName = arguments.getValue("-n", "mega_gfxworker_megacli");
#endif

    config.clientType = arguments.getValue("-c", "default");

    return config;
}

static std::unique_ptr<IGfxProvider> createGfxProvider([[maybe_unused]] const Config& config)
{
#if defined(ENABLE_ISOLATED_GFX)
    GfxIsolatedProcess::Params params{config.endpointName, config.executable};
    if (auto provider = GfxProviderIsolatedProcess::create(params))
    {
        return provider;
    }
#endif
    return IGfxProvider::createInternalGfxProvider();
}

#ifdef ENABLE_SYNC

// converts the given sync configuration to a string
std::string syncConfigToString(const SyncConfig& config)
{
    std::string description(Base64Str<MegaClient::BACKUPHANDLE>(config.mBackupId));
    if (config.getType() == SyncConfig::TYPE_TWOWAY)
    {
        description.append(" TWOWAY");
    }
    else if (config.getType() == SyncConfig::TYPE_UP)
    {
        description.append(" UP");
    }
    else if (config.getType() == SyncConfig::TYPE_DOWN)
    {
        description.append(" DOWN");
    }
    return description;
}

#endif

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
            return "Multi-factor authentication required";
        case API_EMASTERONLY:
            return "Access denied for users";
        case API_EBUSINESSPASTDUE:
            return "Business account has expired";
        case API_EPAYWALL:
            return "Over Disk Quota Paywall";
        case API_ESUBUSERKEYMISSING:
            return "A business error where a subuser has not yet encrypted their master key for "
                   "the admin user and tries to perform a disallowed command (currently u and p)";
        case LOCAL_ENOSPC:
            return "Insufficient disk space";
        default:
            return "Unknown error";
    }
}

string verboseErrorString(error e)
{
    return (string("Error message: ") + errorstring(e)
            + string(" (error code ") + std::to_string(e) + ")");
}

struct ConsoleLock
{
    static std::recursive_mutex outputlock;
    std::ostream& os;
    bool locking = false;
    inline ConsoleLock(std::ostream& o)
        : os(o), locking(true)
    {
        outputlock.lock();
    }
    ConsoleLock(ConsoleLock&& o)
        : os(o.os), locking(o.locking)
    {
        o.locking = false;
    }
    ~ConsoleLock()
    {
        if (locking)
        {
            outputlock.unlock();
        }
    }

    template<class T>
    std::ostream& operator<<(T&& arg)
    {
        return os << std::forward<T>(arg);
    }
};

std::recursive_mutex ConsoleLock::outputlock;

ConsoleLock conlock(std::ostream& o)
{
    // Returns a temporary object that has locked a mutex.  The temporary's destructor will unlock the object.
    // So you can get multithreaded non-interleaved console output with just conlock(cout) << "some " << "strings " << endl;
    // (as the temporary's destructor will run at the end of the outermost enclosing expression).
    // Or, move-assign the temporary to an lvalue to control when the destructor runs (to lock output over several statements).
    // Be careful not to have cout locked across a g_megaApi member function call, as any callbacks that also log could then deadlock.
    return ConsoleLock(o);
}

static error startxfer(TransferDbCommitter& committer, unique_ptr<AppFileGet> file, const string& path, int tag)
{
    error result = API_OK;

    if (client->startxfer(GET, file.get(), committer, false, false, false, NoVersioning, &result, tag))
    {
        file->appxfer_it = appxferq[GET].insert(appxferq[GET].end(), file.get());
        file.release();
    }
    else
    {
        conlock(cout) << "Unable to download file: "
                      << path
                      << " -> "
                      << file->getLocalname().toPath(false)
                      << ": "
                      << errorstring(result)
                      << endl;
    }

    return result;
}

static error startxfer(TransferDbCommitter& committer, unique_ptr<AppFileGet> file, const Node& node, int tag)
{
    return startxfer(committer, std::move(file), node.displaypath(), tag);
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
void AppFileGet::completed(Transfer*, putsource_t source)
{
    if (onCompleted) onCompleted();

    // (at this time, the file has already been placed in the final location)
    delete this;
}

// transfer terminated - too many failures, or unrecoverable failure, or cancelled
void AppFileGet::terminated(error e)
{
    delete this;
}

void AppFilePut::completed(Transfer* t, putsource_t source)
{
    // perform standard completion (place node in user filesystem etc.)
    //File::completed(t, source);

    // standard completion except we want onCompleted to run at the end of putnodes:

    assert(!transfer || t == transfer);
    assert(source == PUTNODES_APP);  // derived class for sync doesn't use this code path
    assert(t->type == PUT);

    auto onCompleted_foward = onCompleted;
    sendPutnodesOfUpload(
        t->client,
        t->uploadhandle,
        *t->ultoken,
        t->filekey,
        source,
        NodeHandle(),
        [onCompleted_foward](const Error& e,
                             targettype_t,
                             vector<NewNode>&,
                             bool targetOverride,
                             int tag,
                             const map<string, string>& /*fileHandles*/)
        {
            if (e)
            {
                cout << "Putnodes error is breaking upload/download cycle: " << e << endl;
            }
            else if (onCompleted_foward)
                onCompleted_foward();
        },
        nullptr,
        false);

    delete this;
}

// transfer terminated - too many failures, or unrecoverable failure, or cancelled
void AppFilePut::terminated(error e)
{
    delete this;
}

AppFileGet::~AppFileGet()
{
    if (appxfer_it != appxferq[GET].end())
        appxferq[GET].erase(appxfer_it);
}

AppFilePut::~AppFilePut()
{
    if (appxfer_it != appxferq[PUT].end())
        appxferq[PUT].erase(appxfer_it);
}

void AppFilePut::displayname(string* dname)
{
    *dname = getLocalname().toName(*transfer->client->fsaccess);
}

// transfer progress callback
void AppFile::progress()
{
}

static void displaytransferdetails(Transfer* t, const string& action)
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

    cout << ": " << (t->type == GET ? "Incoming" : "Outgoing") << " file transfer " << action << ": " << t->localfilename.toPath(false);
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

void DemoApp::transfer_failed(Transfer* t, const Error& e, dstime)
{
    if (e == API_ETOOMANY && e.hasExtraInfo())
    {
        displaytransferdetails(t, "failed (" + getExtraInfoErrorString(e) + ")\n");
    }
    else
    {
        displaytransferdetails(t, "failed (" + string(errorstring(e)) + ")\n");
    }
}

void DemoApp::transfer_complete(Transfer* t)
{
    if (gVerboseMode)
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
}

// transfer about to start - make final preparations (determine localfilename, create thumbnail for image upload)
void DemoApp::transfer_prepare(Transfer* t)
{
    if (gVerboseMode)
    {
        displaytransferdetails(t, "starting\n");
    }

    if (t->type == GET)
    {
        // only set localfilename if the engine has not already done so
        if (t->localfilename.empty())
        {
            t->localfilename = LocalPath::fromAbsolutePath(".");
            t->localfilename.appendWithSeparator(LocalPath::tmpNameLocal(), false);
        }
    }
}

#ifdef ENABLE_SYNC

void DemoApp::syncupdate_stateconfig(const SyncConfig& config)
{
    conlock(cout) << "Sync config updated: " << toHandle(config.mBackupId)
        << " state: " << int(config.mRunState)
        << " error: " << config.mError
        << endl;
}

void DemoApp::sync_added(const SyncConfig& config)
{
    handle backupId = config.mBackupId;
    conlock(cout) << "Sync - added " << toHandle(backupId) << " " << config.getLocalPath().toPath(false) << " enabled: "
        << config.getEnabled() << " syncError: " << config.mError << " " << int(config.mRunState) << endl;
}

void DemoApp::sync_removed(const SyncConfig& config)
{
    conlock(cout) << "Sync - removed: " << toHandle(config.mBackupId) << endl;

}

void DemoApp::syncs_restored(SyncError syncError)
{
    conlock(cout) << "Sync - restoration "
                  << (syncError != NO_SYNC_ERROR ? "failed" : "completed")
                  << ": "
                  << SyncConfig::syncErrorToStr(syncError)
                  << endl;
}

void DemoApp::syncupdate_scanning(bool active)
{
    if (active)
    {
        conlock(cout) << "Sync - scanning local files and folders" << endl;
    }
    else
    {
        conlock(cout) << "Sync - scan completed" << endl;
    }
}

void DemoApp::syncupdate_syncing(bool active)
{
    if (active)
    {
        conlock(cout) << "Sync - syncs are busy" << endl;
    }
    else
    {
        conlock(cout) << "Sync - syncs are idle" << endl;
    }
}

void DemoApp::syncupdate_stalled(bool stalled)
{
    if (stalled)
    {
        conlock(cout) << "Sync - stalled" << endl;
    }
    else
    {
        conlock(cout) << "Sync - stall ended" << endl;
    }
}

void DemoApp::syncupdate_conflicts(bool conflicts)
{
    if (conflicts)
    {
        conlock(cout) << "Sync - conflicting paths detected" << endl;
    }
    else
    {
        conlock(cout) << "Sync - all conflicting paths resolved" << endl;
    }
}

// flags to turn off cout output that can be too volumnous/time consuming
bool syncout_local_change_detection = true;
bool syncout_remote_change_detection = true;
bool syncout_transfer_activity = true;
bool syncout_folder_sync_state = false;

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
        case TREESTATE_IGNORED:
            return "Ignored";
    }

    return "UNKNOWN";
}

void DemoApp::syncupdate_treestate(const SyncConfig &, const LocalPath& lp, treestate_t ts, nodetype_t type)
{
    if (syncout_folder_sync_state)
    {
        if (type != FILENODE)
        {
            conlock(cout) << "Sync - state change of folder " << lp.toPath(false) << " to " << treestatename(ts) << endl;
        }
    }
}
#endif

AppFileGet::AppFileGet(Node* n, NodeHandle ch, const byte* cfilekey, m_off_t csize, m_time_t cmtime, const string* cfilename,
                       const string* cfingerprint, const string& targetfolder)
{
    appxfer_it = appxferq[GET].end();

    if (n)
    {
        h = n->nodeHandle();
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
            memcpy(crc.data(), filekey, sizeof crc);
        }
    }

    string s = targetfolder;
    if (s.empty()) s = ".";
    auto fstype = client->fsaccess->getlocalfstype(LocalPath::fromAbsolutePath(s));

    if (cfilename)
    {
        name = *cfilename;
    }

    auto ln = LocalPath::fromRelativeName(name, *client->fsaccess, fstype);
    ln.prependWithSeparator(LocalPath::fromAbsolutePath(s));
    setLocalname(ln);
}

AppFilePut::AppFilePut(const LocalPath& clocalname, NodeHandle ch, const char* ctargetuser)
{
    appxfer_it = appxferq[PUT].end();

    // full local path
    setLocalname(clocalname);

    // target parent node
    h = ch;

    // target user
    targetuser = ctargetuser;

    name = clocalname.leafName().toName(*client->fsaccess);
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
    cout << "**alert " << b.id << ": " << header << " - " << title << " [at " << displayTime(b.ts()) << "]" << " seen: " << b.seen() << endl;
}

void DemoApp::useralerts_updated(UserAlert::Base** b, int count)
{
    if (b && notifyAlerts)
    {
        for (int i = 0; i < count; ++i)
        {
            if (!b[i]->seen())
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

void DemoApp::chatlinkurl_result (handle chatid, int shard, string* url, string* ct, int numPeers, m_time_t ts, bool meetingRoom, int chatOptions, const std::vector<std::unique_ptr<ScheduledMeeting>>* smList, handle callid, error e)
{
    if (e)
    {
        cout << "URL request for chat-link failed (" << errorstring(e) << ")" << endl;
    }
    else
    {
        ::mega::ChatOptions opts(static_cast<::mega::ChatOptions_t>(chatOptions));
        char idstr[sizeof(handle) * 4 / 3 + 4];
        Base64::btoa((const byte *)&chatid, MegaClient::CHATHANDLE, idstr);
        cout << "Chatid: " << idstr << " (shard " << shard << ")" << endl;
        cout << "URL for chat-link: " << url->c_str() << endl;
        cout << "Encrypted chat-topic: " << ct->c_str() << endl;
        cout << "Creation timestamp: " << ts << endl;
        cout << "Num peers: " << numPeers << endl;
        cout << "Callid: " << Base64Str<MegaClient::CHATHANDLE>(callid) << endl;
        cout << "Meeting room: " << meetingRoom << endl;
        cout << "Waiting room: " << opts.waitingRoom() << endl;
        cout << "Open invite: " << opts.openInvite() << endl;
        cout << "Speak request: " << opts.speakRequest() << endl;
        cout << "Scheduled meeting: " << smList << endl;
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

    cout << "Chat ID: " << Base64Str<sizeof(handle)>(chat->getChatId()) << endl;
    cout << "\tOwn privilege level: " << DemoApp::getPrivilegeString(chat->getOwnPrivileges()) << endl;
    cout << "\tCreation ts: " << chat->getTs() << endl;
    cout << "\tChat shard: " << chat->getShard() << endl;
    cout << "\tGroup chat: " << ((chat->getGroup()) ? "yes" : "no") << endl;
    cout << "\tArchived chat: " << ((chat->isFlagSet(TextChat::FLAG_OFFSET_ARCHIVE)) ? "yes" : "no") << endl;
    cout << "\tPublic chat: " << ((chat->publicChat()) ? "yes" : "no") << endl;
    if (chat->publicChat())
    {
        cout << "\tUnified key: " << chat->getUnifiedKey() << endl;
        cout << "\tMeeting room: " << (chat->getMeeting() ? "yes" : "no") << endl;
    }

    cout << "\tPeers:";

    if (chat->getUserPrivileges())
    {
        cout << "\t\t(userhandle)\t(privilege level)" << endl;
        for (const auto& up : *chat->getUserPrivileges())
        {
            Base64Str<sizeof(handle)> hstr(up.first);
            cout << "\t\t\t" << hstr;
            cout << "\t" << DemoApp::getPrivilegeString(up.second) << endl;
        }
    }
    else
    {
        cout << " no peers (only you as participant)" << endl;
    }
    cout << "\tIs own change: " << (chat->getTag() ? "yes" : "no") << endl;
    if (!chat->getTitle().empty())
    {
        cout << "\tTitle: " << chat->getTitle() << endl;
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

static void setattr_result(NodeHandle, Error e)
{
    if (e)
    {
        cout << "Node attribute update failed (" << errorstring(e) << ")" << endl;
    }
}

static void rename_result(NodeHandle, error e)
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

void DemoApp::fetchnodes_result(const Error& e)
{
    if (e)
    {
        if (e == API_ENOENT && e.hasExtraInfo())
        {
            cout << "File/folder retrieval failed: " << getExtraInfoErrorString(e) << endl;
        }
        else
        {
            cout << "File/folder retrieval failed (" << errorstring(e) << ")" << endl;
        }
        pdf_to_import = false;
    }
    else
    {
        // check if we fetched a folder link and the key is invalid
        if (client->loggedIntoFolder())
        {
            if (client->isValidFolderLink())
            {
                cout << "Folder link loaded correctly." << endl;
            }
            else
            {
                assert(client->nodeByHandle(client->mNodeManager.getRootNodeFiles()));   // node is there, but cannot be decrypted
                cout << "Folder retrieval succeed, but encryption key is wrong." << endl;
            }
        }

        if (pdf_to_import)
        {
            client->importOrDelayWelcomePdf();
        }
        else if (client->shouldWelcomePdfImported())
        {
            client->importWelcomePdfIfDelayed();
        }

        if (client->ephemeralSessionPlusPlus)
        {
            client->putua(ATTR_FIRSTNAME, (const byte*)ephemeralFirstname.c_str(), unsigned(ephemeralFirstname.size()));
            client->putua(ATTR_LASTNAME, (const byte*)ephemeralLastName.c_str(), unsigned(ephemeralLastName.size()));
        }
    }
}

void DemoApp::putnodes_result(const Error& e,
                              targettype_t t,
                              vector<NewNode>& nn,
                              bool targetOverride,
                              int tag,
                              const map<string, string>& /*fileHandles*/)
{
    if (t == USER_HANDLE)
    {
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

    if (targetOverride)
    {
        cout << "Target folder has changed!" << endl;
    }

    auto i = gOnPutNodeTag.find(tag);
    if (i != gOnPutNodeTag.end())
    {
        for (auto &newNode : nn)
        {
            std::shared_ptr<Node> n = client->nodebyhandle(newNode.mAddedHandle);
            if (n)
            {
                i->second(n.get());
            }
        }

        gOnPutNodeTag.erase(i);
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
    std::shared_ptr<Node> n = client->nodebyhandle(h);
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
    cout << "User attribute retrieval failed (" << errorstring(e) << ")" << endl;
}

void DemoApp::getua_result(byte* data, unsigned l, attr_t type)
{
    if (gVerboseMode)
    {
        cout << "Received " << l << " byte(s) of user attribute: ";
        fwrite(data, 1, l, stdout);
        cout << endl;

        if (type == ATTR_ED25519_PUBK)
        {
            cout << "Credentials: " << AuthRing::fingerprint(string((const char*)data, l), true) << endl;
        }
    }

    if (type == ATTR_COOKIE_SETTINGS)
    {
        unsigned long cs = strtoul((const char*)data, nullptr, 10);
        std::bitset<32> bs(cs);
        cout << "Cookie settings = " << cs << " (" << bs << ')' << endl
             << "\tessential: " << bs[0] << endl
             << "\tpreferences: " << bs[1] << endl
             << "\tperformance: " << bs[2] << endl
             << "\tadvertising: " << bs[3] << endl
             << "\tthird party: " << bs[4] << endl;
    }

    if (type == ATTR_FIRSTNAME || type == ATTR_LASTNAME)
    {
        cout << string((char*)data, l) << endl;
    }

    if (type == ATTR_KEYS)
    {
        cout << client->mKeyManager.toString();
    }
}

void DemoApp::getua_result(TLVstore *tlv, attr_t type)
{
    if (!tlv)
    {
        cout << "Error getting private user attribute" << endl;
    }
    else if (!gVerboseMode)
    {
        cout << "Received a TLV with " << tlv->size() << " item(s) of user attribute: " << endl;
        if (type == ATTR_DEVICE_NAMES)
        {
            cout << '(' << (b64driveid.empty() ? "Printing only Device names" :
                (b64driveid == allExtDrives ? "Printing only External-Drive names" :
                    "Printing name of the specified External-Drive only")) << ')' << endl;
        }

        bool printDriveId = false;

        unique_ptr<vector<string>> keys(tlv->getKeys());
        for (auto it = keys->begin(); it != keys->end(); it++)
        {
            const string& key = it->empty() ? "(no key)" : *it;

            // external drive names can be filtered
            if (type == ATTR_DEVICE_NAMES)
            {
                bool isExtDrive = key.rfind(User::attributePrefixInTLV(ATTR_DEVICE_NAMES, true), 0) == 0; // starts with "ext:" prefix
                // print all device names, OR all ext-drive names, OR the name of the selected ext-drive
                printDriveId = (b64driveid.empty() && !isExtDrive) || // device name
                               (isExtDrive && (b64driveid == allExtDrives || key == User::attributePrefixInTLV(ATTR_DEVICE_NAMES, true) + b64driveid));
                if (!printDriveId)
                {
                    continue;
                }
            }

            // print user attribute values
            string value;
            if (!tlv->get(*it, value) || value.empty())
            {
                cout << "\t" << key << "\t" << "(no value)";
            }
            else
            {
                cout << "\t" << key << "\t";
                if (type == ATTR_DEVICE_NAMES || type == ATTR_ALIAS)
                {
                    // Values that are known to contain only printable characters are ok to display directly.
                    cout << value << " (real text value)";
                }
                else
                {
                    // Some values may contain non-printable characters, so display them as base64 encoded.
                    const string& b64value = Base64::btoa(value);
                    cout << b64value << " (base64 encoded value)";
                }
            }

            if (key == client->getDeviceidHash())
            {
                cout << " (own device)";
            }

            cout << endl;
        }

        // echo specific drive name not found
        if (!printDriveId && !b64driveid.empty())
        {
            cout << "Specified drive could not be found" << endl;
        }
        b64driveid.clear(); // in case this was for a request that used it
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

void DemoApp::senddevcommand_result(int value)
{
    cout << "Dev subcommand finished with code: " << value << endl;
}

void exec_devcommand(autocomplete::ACState& s)
{
    const std::string_view subcommand {s.words[1].s};

    std::string email;
    const bool isEmailProvided = s.extractflagparam("-e", email);
    std::string campaign;
    const bool isCampaingProvided = s.extractflagparam("-c", campaign);
    std::string groupId;
    const bool isGroupIdProvided = s.extractflagparam("-g", groupId);

    const auto printElement = [](const auto& p){ std::cout << " " << p; };
    if (subcommand == "abs")
    {
        if (isEmailProvided) std::cout << "devcommand abs will ignore unrequired -e provided\n";

        std::vector<std::string> req;
        if (!isCampaingProvided) req.emplace_back("-c");
        if (!isGroupIdProvided) req.emplace_back("-g");
        if (!req.empty())
        {
            std::cout << "devcommand abs is missing required";
            std::for_each(std::begin(req), std::end(req), printElement);
            std::cout << " options\n";
            return;
        }

        size_t l;
        const int g = std::stoi(groupId, &l); // it's okay throwing in megacli for non-numeric
        if(l != groupId.size())
        {
            std::cout << "abs param -g must be a natural number: " << groupId << " provided\n";
            return;
        }

        client->senddevcommand(subcommand.data(), nullptr, 0, 0, g, campaign.c_str());
    }
    else
    {
        std::vector<std::string> param;
        if (isCampaingProvided) param.emplace_back("-c");
        if (isGroupIdProvided) param.emplace_back("-g");
        if (!param.empty())
        {
            std::cout << "devcommand " << subcommand << " will ignore unrequired";
            std::for_each(std::begin(param), std::end(param), printElement);
            std::cout << " provided options\n";
        }

        client->senddevcommand(subcommand.data(), isEmailProvided ? email.c_str() : nullptr);
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

string DemoApp::getExtraInfoErrorString(const Error& e)
{
    string textError;

    if (e.getUserStatus() == 7)
    {
        textError.append("User status is suspended due to ETD. ");
    }

    textError.append("Link status is: ");
    switch (e.getLinkStatus())
    {
        case 0:
            textError.append("Undeleted");
            break;
        case 1:
            textError.append("Deleted/down");
            break;
        case 2:
            textError.append("Down due to an ETD specifically");
            break;
        default:
            textError.append("Unknown link status");
            break;
    }

    return textError;
}

static void store_line(char*);
static void process_line(char *);
static char* line;

static std::shared_ptr<AccountDetails> account = std::make_shared<AccountDetails>();

// Current remote directory.
static NodeHandle cwd;

// Where we were on the local filesystem when megacli started.
static unique_ptr<LocalPath> startDir(new LocalPath);

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
    if (!client->mNodeManager.getRootNodeFiles().isUndef())
    {
        cout << "ROOT on /" << endl;
    }
    if (!client->mNodeManager.getRootNodeVault().isUndef())
    {
        cout << "VAULT on //in" << endl;
    }
    if (!client->mNodeManager.getRootNodeRubbish().isUndef())
    {
        cout << "RUBBISH on //bin" << endl;
    }

    for (user_map::iterator uit = client->users.begin(); uit != client->users.end(); uit++)
    {
        User* u = &uit->second;
        std::shared_ptr<Node> n;

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

    if (clientFolder && !clientFolder->mNodeManager.getRootNodeFiles().isUndef())
    {
        std::shared_ptr<Node> n = clientFolder->nodeByHandle(clientFolder->mNodeManager.getRootNodeFiles());
        if (n)
        {
            cout << "FOLDERLINK on " << n->displayname() << ":" << endl;
        }
    }
}

bool handles_on = false;
bool showattrs = false;

// returns node pointer determined by path relative to cwd
// path naming conventions:
// * path is relative to cwd
// * /path is relative to ROOT
// * //in is in VAULT (formerly INBOX)
// * //bin is in RUBBISH
// * X: is user X's VAULT (formerly INBOX)
// * X:SHARE is share SHARE from user X
// * Y:name is folder in FOLDERLINK, Y is the public handle
// * : and / filename components, as well as the \, must be escaped by \.
// (correct UTF-8 encoding is assumed)
// returns NULL if path malformed or not found
static std::shared_ptr<Node> nodebypath(const char* ptr, string* user = NULL, string* namepart = NULL)
{
    if (!ptr)
    {
        return nullptr;
    }

    vector<string> c;
    string s;
    size_t l = 0;
    const char* bptr = ptr;
    int remote = 0;
    int folderlink = 0;
    std::shared_ptr<Node> n;
    std::shared_ptr<Node> nn;


    // special case access by handle, same syntax as megacmd
    if (handles_on && ptr && strlen(ptr) == 10 && *ptr == 'H' && ptr[1] == ':')
    {
        handle h8=0;
        Base64::atob(ptr+2, (byte*)&h8, MegaClient::NODEHANDLE);
        return client->nodeByHandle(NodeHandle().set6byte(h8));
    }

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
                        s.append(bptr, static_cast<size_t>(ptr - bptr));
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
                        s.append(bptr, static_cast<size_t>(ptr - bptr));
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

            n = clientFolder->nodeByHandle(clientFolder->mNodeManager.getRootNodeFiles());
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
                        LocalPath::utf8_normalize(&name);
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
                    n = client->nodeByHandle(client->mNodeManager.getRootNodeVault());
                }
                else if (c[2] == "bin")
                {
                    n = client->nodeByHandle(client->mNodeManager.getRootNodeRubbish());
                }
                else
                {
                    return NULL;
                }

                l = 3;
            }
            else
            {
                n = client->nodeByHandle(client->mNodeManager.getRootNodeFiles());

                l = 1;
            }
        }
        else
        {
            n = client->nodeByHandle(cwd);
        }
    }

    // parse relative path
    while (n && l < c.size())
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
                        nn = clientFolder->childnodebyname(n.get(), c[l].c_str());
                    }
                    else
                    {
                        nn = client->childnodebyname(n.get(), c[l].c_str());
                    }

                    if (!nn)
                    {
                        // mv command target? return name part of not found
                        if (namepart && l == c.size() - 1)
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

static void listnodeshares(Node* n, bool printLinks)
{
    if(n->outshares)
    {
        for (share_map::iterator it = n->outshares->begin(); it != n->outshares->end(); it++)
        {
            assert(!it->second->pcr);

            if (printLinks && !it->second->user)
            {
                cout << "\t" << n->displayname();
                cout << ", shared as exported folder link" << endl;
            }

            if (!printLinks && it->second->user)
            {
                cout << "\t" << n->displayname();
                cout << ", shared with " << it->second->user->email << " (" << getAccessLevelStr(it->second->access) << ")"
                     << (client->mKeyManager.isUnverifiedOutShare(n->nodehandle, toHandle(it->second->user->userhandle)) ? " (unverified)" : "")
                     << endl;
            }
        }
    }
}

static void listnodependingshares(Node* n)
{
    if(n->pendingshares)
    {
        for (share_map::iterator it = n->pendingshares->begin(); it != n->pendingshares->end(); it++)
        {
            cout << "\t" << n->displayname();

            assert(it->second->pcr);
            assert(!it->second->user);

            cout << ", pending share with " << it->second->pcr->targetemail << " (" << getAccessLevelStr(it->second->access) << ")"
                 << (client->mKeyManager.isUnverifiedOutShare(n->nodehandle, it->second->pcr->targetemail) ? " (unverified)" : "")
                 << endl;
        }
    }
}

static void listallshares()
{
    cout << "Outgoing shared folders:" << endl;

    sharedNode_vector outshares = client->mNodeManager.getNodesWithOutShares();
    for (auto& share : outshares)
    {
        listnodeshares(share.get(), false);
    }

    cout << "Incoming shared folders:" << endl;

    for (user_map::iterator uit = client->users.begin();
        uit != client->users.end(); uit++)
    {
        User* u = &uit->second;
        std::shared_ptr<Node> n;

        if (u->show == VISIBLE && u->sharing.size())
        {
            cout << "From " << u->email << ":" << endl;

            for (handle_set::iterator sit = u->sharing.begin();
                sit != u->sharing.end(); sit++)
            {
                if ((n = client->nodebyhandle(*sit)))
                {
                    cout << "\t" << n->displayname() << " ("
                        << getAccessLevelStr(n->inshare->access) << ")"
                        << (client->mKeyManager.isUnverifiedInShare(n->nodehandle, u->userhandle) ? " (unverified)" : "")
                        << endl;
                }
            }
        }
    }

    cout << "Pending outgoing shared folders:" << endl;

    // pending outgoing
    sharedNode_vector pendingoutshares = client->mNodeManager.getNodesWithPendingOutShares();
    for (auto& share : pendingoutshares)
    {
        listnodependingshares(share.get());
    }

    cout << "Public folder links:" << endl;

    sharedNode_vector links = client->mNodeManager.getNodesWithLinks();
    for (auto& share : links)
    {
        listnodeshares(share.get(), true);
    }

}

static void dumptree(Node* n, bool recurse, int depth, const char* title, ofstream* toFile)
{
    std::ostream& stream = toFile ? *toFile : cout;
    string titleTmp;

    if (depth)
    {
        if (!toFile)
        {
            if (!title && !(title = n->displayname()))
            {
                title = "CRYPTO_ERROR";
            }

            for (int i = depth; i--; )
            {
                stream << "\t";
            }
        }
        else
        {
            titleTmp = n->displaypath();
            title = titleTmp.c_str();
        }

        stream << title << " (";

        switch (n->type)
        {
            case FILENODE:
            {
                stream << n->size;

                if (handles_on)
                {
                    Base64Str<MegaClient::NODEHANDLE> handlestr(n->nodehandle);
                    stream << " " << handlestr.chars;
                }

                const char* p;
                if ((p = strchr(n->fileattrstring.c_str(), ':')))
                {
                    stream << ", has file attributes " << p + 1;
                }

                if (showattrs && n->attrs.map.size())
                {
                    stream << ", has name";
                    for (auto& a : n->attrs.map)
                    {
                        char namebuf[100]{};
                        AttrMap::nameid2string(a.first, namebuf);
                        stream << " " << namebuf << "=" << a.second;
                    }
                }

                sharedNode_list nodeChildren = client->mNodeManager.getChildren(n);
                if (nodeChildren.size())
                {
                    Node *version = n;
                    int i = 0;
                    while (nodeChildren.size() && (version = nodeChildren.back().get()))
                    {
                        i++;
                        if (handles_on)
                        {
                            if (i == 1) stream << ", has versions: ";

                            Base64Str<MegaClient::NODEHANDLE> handlestr(version->nodehandle);
                            stream << " [" << i << "] " << handlestr.chars;
                        }

                        nodeChildren = client->mNodeManager.getChildren(version);
                    }
                    if (!handles_on)
                    {
                        stream << ", has " << i << " versions";
                    }
                }

                if (n->plink)
                {
                    stream << ", shared as exported";
                    if (n->plink->ets)
                    {
                        stream << " temporal";
                    }
                    else
                    {
                        stream << " permanent";
                    }
                    stream << " file link";
                }

                break;
            }
            case FOLDERNODE:
                if (n->isPasswordNode())            stream << "password entry";
                else if (n->isPasswordNodeFolder()) stream << "password folder";
                else                                stream << "folder";

                if (handles_on)
                {
                    Base64Str<MegaClient::NODEHANDLE> handlestr(n->nodehandle);
                    stream << " " << handlestr.chars;
                }

                if(n->outshares)
                {
                    for (share_map::iterator it = n->outshares->begin(); it != n->outshares->end(); it++)
                    {
                        if (it->first)
                        {
                            stream << ", shared with " << it->second->user->email << ", access "
                                 << getAccessLevelStr(it->second->access);
                        }
                    }

                    if (n->plink)
                    {
                        stream << ", shared as exported";
                        if (n->plink->ets)
                        {
                            stream << " temporal";
                        }
                        else
                        {
                            stream << " permanent";
                        }
                        stream << " folder link";
                    }
                }

                if (n->pendingshares)
                {
                    for (share_map::iterator it = n->pendingshares->begin(); it != n->pendingshares->end(); it++)
                    {
                        if (it->first)
                        {
                            stream << ", shared (still pending) with " << it->second->pcr->targetemail << ", access "
                                 << getAccessLevelStr(it->second->access);
                        }
                    }
                }

                if (n->inshare)
                {
                    stream << ", inbound " << getAccessLevelStr(n->inshare->access) << " share";
                }

                if (showattrs && n->attrs.map.size())
                {
                    stream << ", has name";
                    for (auto& a : n->attrs.map)
                    {
                        char namebuf[100]{};
                        AttrMap::nameid2string(a.first, namebuf);
                        stream << " " << namebuf << "=" << a.second;
                    }
                }

                break;

            default:
                stream << "unsupported type, please upgrade";
        }

        stream << ")" << (n->changed.removed ? " (DELETED)" : "") << endl;

        if (!recurse)
        {
            return;
        }
    }

    if (n->type != FILENODE)
    {
        for (auto& node : client->getChildren(n))
        {
            dumptree(node.get(), recurse, depth + 1, NULL, toFile);
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

static void nodepath(NodeHandle h, string* path)
{
    std::shared_ptr<Node> n = client->nodeByHandle(h);
    *path = n ? n->displaypath() : "";
}

appfile_list appxferq[2];

static const char* prompts[] =
{
    "MEGAcli> ", "Password:", "Old Password:", "New Password:", "Retype New Password:", "Master Key (base64):", "Type 2FA pin:", "Type pin to enable 2FA:", "-Input m to get more, q to quit-"
};

enum prompttype
{
    COMMAND, LOGINPASSWORD, OLDPASSWORD, NEWPASSWORD, PASSWORDCONFIRM, MASTERKEY, LOGINTFA, SETTFA, PAGER
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
    auto cl = conlock(cout); // use this wherever we might have output threading issues

    prompt = p;

    if (p == COMMAND)
    {
        console->setecho(true);
    }
    else if (p == PAGER)
    {
        cout << endl << prompts[p] << flush;
        console->setecho(false); // doesn't seem to do anything
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
    vector<NewNode> nn;
    unsigned nc = 0;
    bool populated = false;


    void allocnodes()
    {
        nn.resize(nc);
        populated = true;
    }

    // determine node tree size (nn = NULL) or write node tree to new nodes array
    void proc(MegaClient* mc, std::shared_ptr<Node> n)
    {
        if (populated)
        {
            string attrstring;
            SymmCipher key;
            NewNode* t = &nn[--nc];

            // copy node
            t->source = NEW_NODE;
            t->type = n->type;
            t->nodehandle = n->nodehandle;
            t->parenthandle = n->parent ? n->parent->nodehandle : UNDEF;

            // copy key (if file) or generate new key (if folder)
            if (n->type == FILENODE)
            {
                t->nodekey = n->nodekey();
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

            t->attrstring.reset(new string);
            tattrs.getjson(&attrstring);
            mc->makeattr(&key, t->attrstring, attrstring.c_str());
        }
        else
        {
            nc++;
        }
    }
};

int loadfile(LocalPath& localPath, string* data)
{
    auto fa = client->fsaccess->newfileaccess();

    if (fa->fopen(localPath, 1, 0, FSLogging::logOnError))
    {
        data->resize(size_t(fa->size));
        fa->fread(data, unsigned(data->size()), 0, 0, FSLogging::logOnError);
        return 1;
    }
    return 0;
}

void xferq(direction_t d, int cancel, bool showActive, bool showAll, bool showCount)
{
    string name;
    int count = 0, activeCount = 0;

    TransferDbCommitter committer(client->tctable);
    for (appfile_list::iterator it = appxferq[d].begin(); it != appxferq[d].end(); )
    {
        if (cancel < 0 || cancel == (*it)->seqno)
        {
            bool active = (*it)->transfer && (*it)->transfer->slot;
            (*it)->displayname(&name);

            if ((active && showActive) || showAll)
            {
                cout << (*it)->seqno << ": " << name;

                if (d == PUT)
                {
                    AppFilePut* f = (AppFilePut*)*it;

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

                if (active)
                {
                    cout << " [ACTIVE] " << ((*it)->transfer->slot->progressreported * 100 / ((*it)->transfer->size ? (*it)->transfer->size : 1)) << "% of " << (*it)->transfer->size;
                }
                cout << endl;
            }

            if (cancel >= 0)
            {
                cout << "Cancelling..." << endl;


                if ((*it)->transfer)
                {
                    client->stopxfer(*it++, &committer);  // stopping calls us back, we delete it, destructor removes it from the map
                }
                continue;
            }

            ++count;
            activeCount += active ? 1 : 0;
        }
        ++it;
    }
    if (showCount)
    {
        cout << "Transfer count: " << count << " active: " << activeCount << endl;
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
        MediaProperties mp = MediaProperties::decodeMediaPropertiesAttributes(n->fileattrstring, (uint32_t*)(n->nodekey().data() + FILENODEKEYLENGTH / 2));
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

#ifndef NO_READLINE

// Where our command history will be recorded.
string historyFile;

void exec_history(autocomplete::ACState& s)
{
    // history clear
    // history list
    // history read file
    // history record file
    // history write file

    // What does the user want to do?
    const auto& command = s.words[1].s;

    // Does the user want to clear their recorded history?
    if (command == "clear")
    {
        if (!historyFile.empty()
            && history_truncate_file(historyFile.c_str(), 0))
        {
            cerr << "Unable to clear recorded history."
                 << endl;
            return;
        }

        // Clear recorded history.
        clear_history();

        // We're done.
        return;
    }

    // Is the user interested in viewing their recorded history?
    if (command == "list")
    {
        auto** history = history_list();

        if (!history)
        {
            cout << "No history has been recorded."
                 << endl;
            return;
        }

        for (auto i = 0; history[i]; ++i)
        {
            cout << i + history_base
                 << ": "
                 << history[i]->line
                 << endl;
        }

        return;
    }

    // Does the user want to load their history from a file?
    if (command == "read")
    {
        if (read_history(s.words[2].s.c_str()))
        {
            cerr << "Unable to read history from: "
                 << s.words[2].s
                 << endl;

            return;
        }

        cout << "Successfully loaded history from: "
             << s.words[2].s
             << endl;

        return;
    }

    // User wants to record history to a file?
    if (command == "record")
    {
        // Clear recorded history.
        clear_history();

        // Truncate history file.
        if (write_history(s.words[2].s.c_str()))
        {
            cerr << "Unable to truncate history file: "
                 << s.words[2].s.c_str();
            return;
        }

        // Remember where we should write the history to.
        historyFile = s.words[2].s;

        cout << "Now recording history to: "
             << historyFile
             << endl;

        return;
    }

    // Only branch left.
    assert(command == "write");

    if (write_history(s.words[2].s.c_str()))
    {
        cerr << "Unable to write history to: "
             << s.words[2].s.c_str();

        return;
    }

    cout << "History written to: "
         << s.words[2].s
         << endl;
}

#endif // ! NO_READLINE

// readline callback - exit if EOF, add to history unless password
#if !defined(WIN32) || !defined(NO_READLINE)
static void store_line(char* l)
{
    if (!l)
    {
#ifndef NO_READLINE
        rl_callback_handler_remove();
#endif /* ! NO_READLINE */

        delete console;
        exit(0);
    }

#ifndef NO_READLINE
    if (*l && prompt == COMMAND)
    {
        char* expansion = nullptr;

        // Try and expand any "event designators."
        auto result = history_expand(l, &expansion);

        // Was the designator bogus?
        if (result < 0)
        {
            add_history(l);

            // Then assume it's a normal command.
            return line = l, void();
        }

        // Otherwise, we have a valid expansion.
        add_history(expansion);

        // Flush the history to disk.
        if (!historyFile.empty())
            write_history(historyFile.c_str());

        // Display but don't execute the expansion.
        if (result == 2)
        {
            cout << expansion << endl;
            return free(expansion);
        }

        // Execute the expansion.
        line = expansion;

        // Release the input string.
        return free(l);
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
    bool procresult(Result r, JSON& json) override
    {
        if (!r.wasErrorOrOK())
        {
            std::vector<string> tempurls;
            bool done = false;
            while (!done)
            {
                switch (json.getnameid())
                {
                case EOO:
                    done = true;
                    break;

                case 'g':
                    if (json.enterarray())   // now that we are requesting v2, the reply will be an array of 6 URLs for a raid download, or a single URL for the original direct download
                    {
                        for (;;)
                        {
                            std::string tu;
                            if (!json.storeobject(&tu))
                            {
                                break;
                            }
                            tempurls.push_back(tu);
                        }
                        json.leavearray();
                        if (tempurls.size() == 6)
                        {
                            if (std::shared_ptr<Node> n = client->nodebyhandle(h))
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
                    // fall-through

                default:
                    json.storeobject();
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
        return true;
    }

private:
    handle h;
    std::shared_ptr<Stack> stack;
};


void getDepthFirstFileHandles(Node* n, deque<handle>& q)
{
    for (auto c : client->getChildren(n))
    {
        if (c->type == FILENODE)
        {
            q.push_back(c->nodehandle);
        }
    }
    for (auto& c : client->getChildren(n))
    {
        if (c->type > FILENODE)
        {
            getDepthFirstFileHandles(c.get(), q);
        }
    }
}

void exec_find(autocomplete::ACState& s)
{
    if (s.words[1].s == "raided")
    {
        if (std::shared_ptr<Node> n = client->nodeByHandle(cwd))
        {
            auto q = std::make_shared<FileFindCommand::Stack>();
            getDepthFirstFileHandles(n.get(), *q);
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

bool recurse_findemptysubfoldertrees(Node* n, bool moveToTrash)
{
    if (n->type == FILENODE)
    {
        return false;
    }

    sharedNode_vector emptyFolders;
    bool empty = true;
    std::shared_ptr<Node> trash = client->nodeByHandle(client->mNodeManager.getRootNodeRubbish());
    sharedNode_list children = client->getChildren(n);
    for (auto& c : children)
    {
        bool subfolderEmpty = recurse_findemptysubfoldertrees(c.get(), moveToTrash);
        if (subfolderEmpty)
        {
            emptyFolders.push_back(c);
        }
        empty = empty && subfolderEmpty;
    }
    if (!empty)
    {
        for (auto& c : emptyFolders)
        {
            if (moveToTrash)
            {
                cout << "moving to trash: " << c->displaypath() << endl;
                client->rename(c, trash, SYNCDEL_NONE, NodeHandle(), nullptr, false, rename_result);
            }
            else
            {
                cout << "empty folder tree at: " << c->displaypath() << endl;
            }
        }
    }
    return empty;
}

void exec_findemptysubfoldertrees(autocomplete::ACState& s)
{
    bool moveToTrash = s.extractflag("-movetotrash");
    if (std::shared_ptr<Node> n = client->nodeByHandle(cwd))
    {
        if (recurse_findemptysubfoldertrees(n.get(), moveToTrash))
        {
            cout << "the search root path only contains empty folders: " << n->displaypath() << endl;
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

    std::string path = p.u8string();
    auto fileSystemType = client->fsaccess->getlocalfstype(LocalPath::fromAbsolutePath(path));
    multimap<string, shared_ptr<Node> > ms;
    multimap<string, fs::path> ps;
    for (auto& m : client->getChildren(mn))
    {
        string leafname = m->displayname();
        client->fsaccess->escapefsincompatible(&leafname, fileSystemType);
        ms.emplace(leafname, m);
    }
    for (fs::directory_iterator pi(p); pi != fs::directory_iterator(); ++pi)
    {
        auto leafname = pi->path().filename().u8string();
        client->fsaccess->escapefsincompatible(&leafname, fileSystemType);
        ps.emplace(leafname, pi->path());
    }

    for (auto p_iter = ps.begin(); p_iter != ps.end(); )
    {
        auto er = ms.equal_range(p_iter->first);
        auto next_p = p_iter;
        ++next_p;
        for (auto i = er.first; i != er.second; ++i)
        {
            if (recursiveCompare(i->second.get(), p_iter->second))
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
std::shared_ptr<Node> nodeFromRemotePath(const string& s)
{
    std::shared_ptr<Node> n;
    if (s.empty())
    {
        n = client->nodeByHandle(cwd);
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

#ifdef MEGA_MEASURE_CODE

void exec_deferRequests(autocomplete::ACState& s)
{
    // cause all the API requests of this type to be gathered up so they will be sent in a single batch, for timing purposes
    bool putnodes = s.extractflag("-putnodes");
    bool movenode = s.extractflag("-movenode");
    bool delnode = s.extractflag("-delnode");

    client->reqs.deferRequests =    [=](Command* c)
                                    {
                                        return  (putnodes && dynamic_cast<CommandPutNodes*>(c)) ||
                                                (movenode && dynamic_cast<CommandMoveNode*>(c)) ||
                                                (delnode && dynamic_cast<CommandDelNode*>(c));
                                    };
}

void exec_sendDeferred(autocomplete::ACState& s)
{
    // send those gathered up commands, and optionally reset the gathering
    client->reqs.sendDeferred();

    if (s.extractflag("-reset"))
    {
        client->reqs.deferRequests = nullptr;
    }
}

void exec_codeTimings(autocomplete::ACState& s)
{
    bool reset = s.extractflag("-reset");
    cout << client->performanceStats.report(reset, client->httpio, client->waiter.get(), client->reqs) << flush;
}

#endif

std::function<void()> onCompletedUploads;

void setAppendAndUploadOnCompletedUploads(string local_path, int count, bool allowDuplicateVersions)
{

    onCompletedUploads = [local_path, count, allowDuplicateVersions](){

        {
            ofstream f(local_path, std::ios::app);
            f << count << endl;
        }
        cout << count << endl;

        TransferDbCommitter committer(client->tctable);
        int total = 0;
        auto lp = LocalPath::fromAbsolutePath(local_path);
        uploadLocalPath(FILENODE, lp.leafName().toPath(false), lp, client->nodeByHandle(cwd).get(), "", committer, total, false, ClaimOldVersion, nullptr, false, allowDuplicateVersions);

        if (count > 0)
        {
            setAppendAndUploadOnCompletedUploads(local_path, count-1, allowDuplicateVersions);
        }
        else
        {
            onCompletedUploads = nullptr;
        }
    };
}

std::deque<std::function<void()>> mainloopActions;

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
    std::shared_ptr<Node> n = nodeFromRemotePath(s.words[2].s);
    if (n && !p.empty())
    {
        recursiveCompare(n.get(), p);
    }
}


bool buildLocalFolders(fs::path targetfolder, const string& prefix, int foldersperfolder, int recurselevel, int filesperfolder, uint64_t filesize, int& totalfilecount, int& totalfoldercount, vector<LocalPath>* localPaths)
{
    fs::path p = targetfolder / fs::u8path(prefix);
    if (!fs::is_directory(p) && !fs::create_directory(p))
        return false;
    ++totalfoldercount;

    for (int i = 0; i < filesperfolder; ++i)
    {
        string filename = prefix + "_file_" + std::to_string(++totalfilecount);
        fs::path fp = p / fs::u8path(filename);
        if (localPaths) localPaths->push_back(LocalPath::fromAbsolutePath(fp.u8string()));
        ofstream fs(fp.u8string(), std::ios::binary);
        char buffer[64 * 1024];
        fs.rdbuf()->pubsetbuf(buffer, sizeof(buffer));

        int counter = totalfilecount;
        for (auto j = filesize / sizeof(int); j--; )
        {
            fs.write((char*)&counter, sizeof(int));
            ++counter;
        }
        fs.write((char*)&counter, filesize % sizeof(int));
    }

    if (recurselevel > 1)
    {
        for (int i = 0; i < foldersperfolder; ++i)
        {
            if (!buildLocalFolders(p, prefix + "_" + std::to_string(i), foldersperfolder, recurselevel - 1, filesperfolder, filesize, totalfilecount, totalfoldercount, nullptr))
                return false;
        }
    }
    return true;
}

void exec_generatetestfilesfolders(autocomplete::ACState& s)
{
    string param, nameprefix = "test";
    int folderdepth = 1, folderwidth = 1, filecount = 100;
    int64_t filesize = 1024;
    if (s.extractflagparam("-folderdepth", param)) folderdepth = atoi(param.c_str());
    if (s.extractflagparam("-folderwidth", param)) folderwidth = atoi(param.c_str());
    if (s.extractflagparam("-filecount", param)) filecount = atoi(param.c_str());
    if (s.extractflagparam("-filesize", param)) filesize = atoll(param.c_str());
    if (s.extractflagparam("-nameprefix", param)) nameprefix = param;

    fs::path p = pathFromLocalPath(s.words[1].s, true);
    if (!p.empty())
    {
        int totalfilecount = 0, totalfoldercount = 0;
        buildLocalFolders(p,
                          nameprefix,
                          folderwidth,
                          folderdepth,
                          filecount,
                          static_cast<uint64_t>(filesize),
                          totalfilecount,
                          totalfoldercount,
                          nullptr);
        cout << "created " << totalfilecount << " files and " << totalfoldercount << " folders" << endl;
    }
    else
    {
        cout << "invalid directory: " << p.u8string() << endl;
    }
}

map<string, int> cycleUploadChunkFails;
map<string, int> cycleDownloadFails;

void checkReportCycleFails()
{
    for (auto& i : cycleDownloadFails) cout << i.first << " " << i.second;
    for (auto& i : cycleUploadChunkFails) cout << i.first << " " << i.second;
}

std::shared_ptr<Node> cycleUploadDownload_cloudWorkingFolder = nullptr;
void cycleDownload(LocalPath lp, int count);
void cycleUpload(LocalPath lp, int count)
{
    checkReportCycleFails();
    TransferDbCommitter committer(client->tctable);

    LocalPath upload_lp = lp;
    upload_lp.append(LocalPath::fromRelativePath("_" + std::to_string(count)));
    string leaf = upload_lp.leafName().toPath(false);

    int total = 0;
    uploadLocalPath(FILENODE, leaf, upload_lp, cycleUploadDownload_cloudWorkingFolder.get(), "", committer, total, false, NoVersioning,
        [lp, count](LocalPath)
        {
            return [lp, count]()
                {
                    cycleDownload(lp, count);
                };
        }, true, true);

    // also delete the old remote file
    if (count > 0)
    {
        string leaf2 = lp.leafName().toPath(false) + "_" + std::to_string(count-1);
        if (std::shared_ptr<Node> lastuploaded = client->childnodebyname(cycleUploadDownload_cloudWorkingFolder.get(), leaf2.c_str(), true))
        {
            client->unlink(lastuploaded.get(), false, client->nextreqtag(), false, nullptr);
        }
    }

}

void cycleDownload(LocalPath lp, int count)
{
    checkReportCycleFails();

    string leaf = lp.leafName().toPath(false) + "_" + std::to_string(count);

    std::shared_ptr<Node> uploaded = client->childnodebyname(cycleUploadDownload_cloudWorkingFolder.get(), leaf.c_str(), true);

    if (!uploaded)
    {
        cout << "Uploaded file " << leaf << " not found, cycle broken" << endl;
        return;
    }

    LocalPath downloadName = lp;
    downloadName.append(LocalPath::fromRelativePath("_" + std::to_string(count+1)));


    string newleaf = lp.leafName().toPath(false);
    newleaf += "_" + std::to_string(count + 1);

    auto f = new AppFileGet(uploaded.get(), NodeHandle(), NULL, -1, 0, &newleaf, NULL, lp.parentPath().toPath(false));
    f->noRetries = true;

    f->onCompleted = [lp, count]()
    {
        cycleUpload(lp, count+1);
    };

    f->appxfer_it = appxferq[GET].insert(appxferq[GET].end(), f);
    TransferDbCommitter committer(client->tctable);
    client->startxfer(GET, f, committer, false, false, false, NoVersioning, nullptr, client->nextreqtag());

    // also delete the old local file
    lp.append(LocalPath::fromRelativePath("_" + std::to_string(count)));
    client->fsaccess->unlinklocal(lp);
}

int gap_resumed_uploads = 0;

void exec_cycleUploadDownload(autocomplete::ACState& s)
{

#ifdef MEGASDK_DEBUG_TEST_HOOKS_ENABLED
    globalMegaTestHooks.onUploadChunkFailed = [](error e)
        {
            ++cycleUploadChunkFails["upload-chunk-err-" + std::to_string(int(e))];
        };
    globalMegaTestHooks.onDownloadFailed = [](error e)
        {
            if (e != API_EINCOMPLETE)
            {
                ++cycleDownloadFails["download-err-" + std::to_string(int(e))];
            }
        };

    globalMegaTestHooks.onUploadChunkSucceeded = [](Transfer* t, TransferDbCommitter& committer)
        {
            if (t->chunkmacs.hasUnfinishedGap(1024ll*1024*1024*1024*1024))
            //if (t->pos > 5000000 && rand() % 2 == 0)
            {
                ++gap_resumed_uploads;

                // simulate this transfer
                string serialized;
                t->serialize(&serialized);

                // put the transfer in cachedtransfers so we can resume it
                Transfer::unserialize(client, &serialized, client->multi_cachedtransfers);

                // prep to try to resume this upload after we get back to our main loop
                auto fpstr = t->files.front()->getLocalname().toPath(false);
                auto countpos = fpstr.find_last_of('_');
                auto count = atoi(fpstr.c_str() + countpos + 1);
                fpstr.resize(countpos);

                mainloopActions.push_back([fpstr, count](){ cycleUpload(LocalPath::fromAbsolutePath(fpstr), count); });

                //terminate this transfer
                t->failed(API_EINCOMPLETE, committer);
                return false; // exit doio() for this transfer
            }
            return true;
        };
#endif

    string param, nameprefix = "cycleUpDown";
    int filecount = 10;
    int64_t filesize = 305560;
    if (s.extractflagparam("-filecount", param)) filecount = atoi(param.c_str());
    if (s.extractflagparam("-filesize", param)) filesize = atoll(param.c_str());
    if (s.extractflagparam("-nameprefix", param)) nameprefix = param;

    fs::path p = pathFromLocalPath(s.words[1].s, true);
    cycleUploadDownload_cloudWorkingFolder = nodeFromRemotePath(s.words[2].s);

    if (!p.empty())
    {
        int totalfilecount = 0, totalfoldercount = 0;
        vector<LocalPath> localPaths;
        buildLocalFolders(p,
                          nameprefix,
                          1,
                          1,
                          filecount,
                          static_cast<uint64_t>(filesize),
                          totalfilecount,
                          totalfoldercount,
                          &localPaths);
        cout << "created " << totalfilecount << " files and " << totalfoldercount << " folders" << endl;

        for (auto& fp : localPaths)
        {
            LocalPath startPath = fp;
            startPath.append(LocalPath::fromRelativePath("_0"));
            client->fsaccess->renamelocal(fp, startPath, true);
            cycleUpload(fp, 0);
        }
    }
    else
    {
        cout << "invalid directory: " << p.u8string() << endl;
    }
}


void exec_generate_put_fileversions(autocomplete::ACState& s)
{
    int count = 100;
    string param;
    if (s.extractflagparam("-count", param)) count = atoi(param.c_str());

    setAppendAndUploadOnCompletedUploads(s.words[1].s, count, true);
    onCompletedUploads();
}

void exec_generatesparsefile(autocomplete::ACState& s)
{
    int64_t filesize = int64_t(2) * 1024 * 1024 * 1024 * 1024;
    string param;
    if (s.extractflagparam("-filesize", param)) filesize = atoll(param.c_str());

    fs::path p = pathFromLocalPath(s.words[1].s, false);
    std::ofstream(p).put('a');
    cout << "File size:  " << fs::file_size(p) << '\n'
        << "Free space: " << fs::space(p).free << '\n';

#ifdef WIN32
    HANDLE hFile = CreateFileW((LPCWSTR)p.u16string().data(),
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_WRITE | FILE_SHARE_READ,
        NULL,
        OPEN_ALWAYS,
        0,
        NULL);
    DWORD bytesReturned = 0;
    if (!DeviceIoControl(
        hFile,                             // handle to a file
        FSCTL_SET_SPARSE,                  // dwIoControlCode
        (PFILE_SET_SPARSE_BUFFER) NULL,    // input buffer
        (DWORD) 0,                         // size of input buffer
        NULL,                              // lpOutBuffer
        0,                                 // nOutBufferSize
        &bytesReturned,                    // number of bytes returned
        NULL))                              // OVERLAPPED structure
    {
        cout << "Set sparse file operation failed." << endl;
    }
    CloseHandle(hFile);
#endif //WIN32

    fs::resize_file(p, static_cast<std::uintmax_t>(filesize));
    cout << "File size:  " << fs::file_size(p) << '\n'
        << "Free space: " << fs::space(p).free << '\n';
}

void exec_lreplace(autocomplete::ACState& s)
{
    bool file = s.extractflag("-file");
    bool folder = s.extractflag("-folder");

    fs::path p = pathFromLocalPath(s.words[1].s, true);

    // replace (or create) a file/folder - this is to test a changed fsid in sync code
    if (file)
    {
        string content = s.words[2].s;
        ofstream f(p);
        f << content;
    }
    else if (folder)
    {
        if (fs::exists(p)) fs::remove(p);
        fs::create_directory(p);
    }
}

void exec_lrenamereplace(autocomplete::ACState& s)
{
    bool file = s.extractflag("-file");
    bool folder = s.extractflag("-folder");

    fs::path p = pathFromLocalPath(s.words[1].s, true);
    string content = s.words[2].s;
    fs::path p2 = pathFromLocalPath(s.words[3].s, false);

    // replace (or create) a file/folder - this is to test a changed fsid in sync code
    fs::rename(p, p2);
    if (file)
    {
        ofstream f(p);
        f << content;
    }
    else if (folder)
    {
        fs::create_directory(p);
    }
}

#endif

void exec_getcloudstorageused(autocomplete::ACState&)
{
    if (client->loggedin() != FULLACCOUNT && !client->loggedIntoFolder())
    {
        cout << "Not logged in" << endl;
        return;
    }

    NodeCounter nc = client->mNodeManager.getCounterOfRootNodes();
    cout << "Total cloud storage: " << nc.storage + nc.versionStorage << " bytes" << endl;
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

    client->getaccountdetails(std::make_shared<AccountDetails>(), storage, transfer, pro, false, false, false, -1);
}

void exec_getuserdata(autocomplete::ACState&)
{
    if (client->loggedin()) client->getuserdata(client->reqtag);
    else client->getmiscflags();
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
    if (const std::shared_ptr<Node> n = nodeFromRemotePath(s.words[1].s))
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

void printAuthringInformation(handle userhandle)
{
    for (auto &it : client->mAuthRings)
    {
        AuthRing &authring = it.second;
        attr_t at = it.first;
        cout << User::attr2string(at) << ": " << endl;
        for (auto &uh : authring.getTrackedUsers())
        {
            if (uh == userhandle || ISUNDEF(userhandle))    // no user was typed --> show authring for all users
            {
                User *user = client->finduser(uh);
                string email = user ? user->email : "not a contact";

                cout << "\tUserhandle: \t" << Base64Str<MegaClient::USERHANDLE>(uh) << endl;
                cout << "\tEmail:      \t" << email << endl;
                cout << "\tFingerprint:\t" << Utils::stringToHex(authring.getFingerprint(uh)) << endl;
                cout << "\tAuth. level: \t" << AuthRing::authMethodToStr(authring.getAuthMethod(uh)) << endl;
            }
        }
    }
}

void exec_setmaxconnections(autocomplete::ACState& s)
{
    auto direction = s.words[1].s == "put" ? PUT : GET;
    if (s.words.size() == 3)
    {
        client->setmaxconnections(direction, atoi(s.words[2].s.c_str()));
    }
    cout << "connections: " << (int)client->connections[direction] << endl;
}


class MegaCLILogger : public ::mega::Logger {
public:
    ofstream mLogFile;
    string mLogFileName;
    bool logToConsole = false;

    void log(const char*, int loglevel, const char*, const char *message
#ifdef ENABLE_LOG_PERFORMANCE
                 , const char **directMessages, size_t *directMessagesSizes, unsigned numberMessages
#endif
    ) override
    {
        using namespace std::chrono;
        auto et =system_clock::now().time_since_epoch();
        auto millisec_since_epoch =  duration_cast<milliseconds>(et).count();
        auto sec_since_epoch = duration_cast<seconds>(et).count();
        char ts[50];
        auto t = std::time(NULL);
        t = (m_time_t) sec_since_epoch;
        if (!std::strftime(ts, sizeof(ts), "%H:%M:%S", std::localtime(&t)))
        {
            ts[0] = '\0';
        }

        auto ms = std::to_string(unsigned(millisec_since_epoch - 1000*sec_since_epoch));
        string s;
        s.reserve(1024);
        s += ts;
        s += "." + string(3 - std::min<size_t>(3, ms.size()), '0') + ms;
        s += " ";
        if (message) s += message;
#ifdef ENABLE_LOG_PERFORMANCE
        for (unsigned i = 0; i < numberMessages; ++i) s.append(directMessages[i], directMessagesSizes[i]);
#endif

        if (logToConsole)
        {
            std::cout << s << std::endl;
        }

        if (mLogFile.is_open())
        {
            mLogFile << s << std::endl;
        }

#ifdef WIN32
        // Supply the log strings to Visual Studio Output window, regardless of toconsole/file settings
        s += "\r\n";
        OutputDebugStringA(s.c_str());
#endif
    }
};

LocalPath localPathArg(string s)
{
    if (s.empty()) return LocalPath();
    return LocalPath::fromAbsolutePath(s);
}

void exec_fingerprint(autocomplete::ACState& s)
{
    auto localfilepath = localPathArg(s.words[1].s);
    auto fa = client->fsaccess->newfileaccess();

    if (fa->fopen(localfilepath, true, false, FSLogging::logOnError, nullptr))
    {
        FileFingerprint fp;
        fp.genfingerprint(fa.get());
        cout << Utils::stringToHex(std::string((const char*)&fp.size, sizeof(fp.size))) << "/" <<
                Utils::stringToHex(std::string((const char*)&fp.mtime, sizeof(fp.mtime))) << "/" <<
                Utils::stringToHex(std::string((const char*)&fp.crc, sizeof(fp.crc))) << endl;
    }
    else
    {
        cout << "Failed to open: " << s.words[1].s << endl;
    }
}

void exec_showattrs(autocomplete::ACState& s)
{
    if (s.words.size() == 2)
    {
        if (s.words[1].s == "on")
        {
            showattrs = true;
        }
        else if (s.words[1].s == "off")
        {
            showattrs = false;
        }
        else
        {
            cout << "invalid showattrs setting" << endl;
        }
    }
    else
    {
        cout << "      showattrs on|off " << endl;
    }
}

void exec_timelocal(autocomplete::ACState& s)
{
    bool get = s.words[1].s == "get";
    auto localfilepath = localPathArg(s.words[2].s);

    if ((get && s.words.size() != 3) || (!get && s.words.size() != 4))
    {
        cout << "wrong number of arguments for : " << s.words[1].s << endl;
        return;
    }

    m_time_t set_time = 0;

    if (!get)
    {
        // similar to Transfers::complete()

        std::istringstream is(s.words[3].s);
        std::tm tm_record;
        is >> std::get_time(&tm_record, "%Y-%m-%d %H:%M:%S");

        set_time = m_mktime(&tm_record);

        cout << "Setting mtime to " << set_time << endl;

        bool success = client->fsaccess->setmtimelocal(localfilepath, set_time);
        if (!success)
        {
            cout << "setmtimelocal failed!  Was it transient? " << client->fsaccess->transient_error << endl;
        }
    }

    // perform get in both cases
    auto fa = client->fsaccess->newfileaccess();
    if (fa->fopen(localfilepath, true, false, FSLogging::logOnError))
    {
        FileFingerprint fp;
        fp.genfingerprint(fa.get());
        if (fp.isvalid)
        {
            std::tm tm_record;
            m_localtime(fp.mtime, &tm_record);
            cout << "mtime for file is " << fp.mtime << ": " << std::put_time(&tm_record, "%Y-%m-%d %H:%M:%S") << endl;

            if (!get)
            {
                if (abs(set_time - fp.mtime) <= 2)
                {
                    cout << "mtime read back is within 2 seconds, so success. Actual difference: " << abs(set_time - fp.mtime) << endl;
                }
                else
                {
                    cout << "ERROR Silent failure in setmtimelocal, difference is " << abs(set_time - fp.mtime) << endl;
                }
            }
        }
        else
        {
            cout << "fingerprint generation failed: " << localfilepath.toPath(false) << endl;
        }
    }
    else
    {
        cout << "fopen failed: " << localfilepath.toPath(false) << endl;
    }

}

void putua_map(const std::string& b64key, const std::string& b64value, attr_t attrtype)
{
    User* ownUser = client->ownuser();
    if (!ownUser)
    {
        cout << "Must be logged in to set own attributes." << endl;
        return;
    }

    std::unique_ptr<TLVstore> tlv;

    const UserAttribute* attribute = ownUser->getAttribute(attrtype);
    if (!attribute || attribute->isNotExisting()) // attr doesn't exist -> create it
    {
        tlv.reset(new TLVstore());
        const string& realValue = Base64::atob(b64value);
        tlv->set(b64key, realValue); // real value, non-B64
    }
    else if (attribute->isExpired())
    {
        cout << "User attribute is outdated";
        cout << "Fetch the attribute first" << endl;
        return;
    }
    else
    {
        tlv.reset(TLVstore::containerToTLVrecords(&attribute->value(), &client->key));

        string_map attrMap;
        attrMap[b64key] = b64value; // User::mergeUserAttribute() expects B64 values
        if (!User::mergeUserAttribute(attrtype, attrMap, *tlv.get()))
        {
            cout << "Failed to merge with existing values" << endl;
            return;
        }
    }

    // serialize and encrypt the TLV container
    std::unique_ptr<std::string> container(tlv->tlvRecordsToContainer(client->rng, &client->key));
    client->putua(attrtype, (byte*)container->data(), unsigned(container->size()));
}

void exec_setdevicename(autocomplete::ACState& s)
{
    const string& b64idhash = client->getDeviceidHash(); // already in B64
    const string& devname = s.words[1].s;
    const string& b64devname = Base64::btoa(devname);
    putua_map(b64idhash, b64devname, ATTR_DEVICE_NAMES);
}

void exec_getdevicename(autocomplete::ACState& s)
{
    User* u = client->ownuser();
    if (!u)
    {
        cout << "Must be logged in to query own attributes." << endl;
        return;
    }
    b64driveid.clear(); // filter out all external drives

    client->getua(u, ATTR_DEVICE_NAMES);
}

void exec_setextdrivename(autocomplete::ACState& s)
{
    const string& drivepath = s.words[1].s;
    const string& drivename = s.words[2].s;

    // check if the drive-id was already created
    // read <drivepath>/.megabackup/drive-id
    handle driveid;
    error e = readDriveId(*client->fsaccess, drivepath.c_str(), driveid);

    if (e == API_ENOENT)
    {
        // generate new id
        driveid = generateDriveId(client->rng);
        // write <drivepath>/.megabackup/drive-id
        e = writeDriveId(*client->fsaccess, drivepath.c_str(), driveid);
    }

    if (e != API_OK)
    {
        cout << "Failed to get drive-id for " << drivepath << endl;
        return;
    }

    putua_map(User::attributePrefixInTLV(ATTR_DEVICE_NAMES, true) + string(Base64Str<MegaClient::DRIVEHANDLE>(driveid)),
              Base64::btoa(drivename), ATTR_DEVICE_NAMES);
}

void exec_getextdrivename(autocomplete::ACState& s)
{
    User* u = client->ownuser();
    if (!u)
    {
        cout << "Must be logged in to query own attributes." << endl;
        return;
    }

    bool idFlag = s.extractflag("-id");
    bool pathFlag = s.extractflag("-path");
    b64driveid = allExtDrives; // list all external drives

    if (s.words.size() == 2)
    {
        if (idFlag)
        {
            b64driveid = s.words[1].s;
        }
        else if (pathFlag)
        {
            // read drive-id from <drivepath>/.megabackup/drive-id
            const string& drivepath = s.words[1].s;
            handle driveid = 0;
            error e = readDriveId(*client->fsaccess, drivepath.c_str(), driveid);

            if (e == API_ENOENT)
            {
                cout << "Drive-id not set for " << drivepath << endl;
                return;
            }

            b64driveid = string(Base64Str<MegaClient::DRIVEHANDLE>(driveid));
        }
    }

    client->getua(u, ATTR_DEVICE_NAMES);
}

void exec_setmybackups(autocomplete::ACState& s)
{
    const string& bkpsFolder = s.words[1].s;
    std::function<void(Error)> completion = [bkpsFolder](Error e)
    {
        if (e == API_OK)
        {
            cout << "\"My Backups\" folder set to " << bkpsFolder << endl;
        }
        else
        {
            cout << "Failed to set \"My Backups\" folder to " << bkpsFolder << " (remote error " << error(e) << ": " << errorstring(e) << ')' << endl;
        }
    };

    error err = client->setbackupfolder(bkpsFolder.c_str(), 0, completion);
    if (err != API_OK)
    {
        cout << "Failed to set \"My Backups\" folder to " << bkpsFolder << " (" << err << ": " << errorstring(err) << ')' << endl;
    }
}

void exec_getmybackups(autocomplete::ACState&)
{
    User* u = client->ownuser();
    if (!u)
    {
        cout << "Login first." << endl;
        return;
    }

    const UserAttribute* attribute = u->getAttribute(ATTR_MY_BACKUPS_FOLDER);
    if (!attribute || attribute->isNotExisting())
    {
        cout << "\"My Backups\" folder has not been set." << endl;
        return;
    }

    handle h = 0;
    memcpy(&h, attribute->value().data(), MegaClient::NODEHANDLE);
    if (!h || h == UNDEF)
    {
        cout << "Invalid handle stored for \"My Backups\" folder." << endl;
        return;
    }

    std::shared_ptr<Node> n = client->nodebyhandle(h);
    if (!n)
    {
        cout << "\"My Backups\" folder could not be found." << toHandle(h) << endl;
        return;
    }

    cout << "\"My Backups\" folder (handle " << toHandle(h) << "): " << n->displaypath() << endl;
}

#ifdef ENABLE_SYNC
void exec_backupcentreUpdateState(const string& backupIdStr, CommandBackupPut::SPState newState)
{
    handle backupId = 0;
    Base64::atob(backupIdStr.c_str(), (byte*)&backupId, MegaClient::BACKUPHANDLE);

    // determine if it's a backup or other type of sync
    SyncConfig c;
    bool found = client->syncs.configById(backupId, c);
    string syncType = found && c.isBackup() ? "backup" : "sync";

    client->updateStateInBC(backupId,
                            newState,
                            [newState, syncType, backupId](const Error& e)
                            {
                                string newStateStr =
                                    newState == CommandBackupPut::TEMPORARY_DISABLED ? "pause" :
                                                                                       "resume";
                                if (e == API_OK)
                                {
                                    cout << "Backup Centre - " << newStateStr << "d " << syncType
                                         << ' ' << toHandle(backupId) << endl;
                                }
                                else
                                {
                                    cout << "Backup Centre - Failed to " << newStateStr << ' '
                                         << syncType << ' ' << toHandle(backupId) << " ("
                                         << errorstring(e) << ')' << endl;
                                }
                            });
}

void exec_backupcentre(autocomplete::ACState& s)
{
    bool delFlag = s.extractflag("-del");
    bool purgeFlag = s.extractflag("-purge");
    bool stopFlag = s.extractflag("-stop");
    bool pauseFlag = s.extractflag("-pause");
    bool resumeFlag = s.extractflag("-resume");

    if (s.words.size() == 1)
    {
        client->getBackupInfo([purgeFlag](const Error& e, const vector<CommandBackupSyncFetch::Data>& data)
        {
            if (e)
            {
                cout << "Backup Center - failed to get info about Backups: " << e << endl;
            }
            else
            {
                for (auto& d : data)
                {
                    if (purgeFlag)
                    {
                        client->reqs.add(new CommandBackupRemove(client, d.backupId,[&](Error e)
                        {
                            if (e)
                            {
                                cout << "Backup Center - failed to purge id: " << toHandle(d.backupId) << endl;
                            }
                        }));

                    }
                    else
                    {
                        cout << "Backup ID: " << toHandle(d.backupId) << " (" << d.backupId << ')' << endl;
                        cout << "  backup type: " << backupTypeToStr(d.backupType) << endl;
                        cout << "  root handle: " << toNodeHandle(d.rootNode) << endl;
                        cout << "  local folder: " << d.localFolder << endl;
                        cout << "  device id: " << d.deviceId << endl;
                        cout << "  device user-agent: " << d.deviceUserAgent << endl;
                        cout << "  sync state: " << d.syncState << endl;
                        cout << "  sync substate: " << d.syncSubstate << endl;
                        cout << "  extra: " << d.extra << endl;
                        cout << "    backup name: " << d.backupName << endl;
                        cout << "  heartbeat timestamp: " << d.hbTimestamp << endl;
                        cout << "  heartbeat status: " << d.hbStatus << endl;
                        cout << "  heartbeat progress: " << d.hbProgress << endl;
                        cout << "  heartbeat uploads: " << d.uploads << endl;
                        cout << "  heartbeat downloads: " << d.downloads << endl;
                        cout << "  last activity time: " << d.lastActivityTs << endl;
                        cout << "  last node handle: " << toNodeHandle(d.lastSyncedNodeHandle) << endl << endl;
                    }
                }

                if (purgeFlag)
                {
                    cout << "Backup Center - Purging registered syncs/backups from API..." << endl;
                }
                else
                {
                    cout << "Backup Centre - Sync / backup count: " << data.size() << endl;
                }
             }
        });
    }
    else if ((delFlag && s.words.size() >= 2) || // remove backup && (move or delete) its contents
             (stopFlag && s.words.size() == 2))  // stop non-backup sync
    {
        handle backupId = 0;
        const string& backupIdStr = s.words[1].s;
        Base64::atob(backupIdStr.c_str(), (byte*)&backupId, MegaClient::BACKUPHANDLE);

        // get move destination for the removed backup
        handle hDest = 0;
        if (delFlag && s.words.size() == 3)
        {
            Base64::atob(s.words[2].s.c_str(), (byte*)&hDest, MegaClient::NODEHANDLE);

            // validation
            std::shared_ptr<Node> targetDest = client->nodebyhandle(hDest);
            if (!targetDest)
            {
                cout << "Backup Centre - Move destination " << s.words[2].s << " not found" << endl;
                return;
            }
        }
        else
        {
            hDest = UNDEF;
        }

        // determine if it's a backup or other type of sync
        SyncConfig c;
        bool found = client->syncs.configById(backupId, c);
        bool isBackup = found && c.isBackup();

        // request removal
        client->removeFromBC(backupId, hDest, [backupId, isBackup, hDest](const Error& e)
        {
            if (e == API_OK)
            {
                cout << "Backup Centre - " << (isBackup ? "Backup " : "Sync ") << toHandle(backupId);
                if (isBackup)
                {
                    cout << " removed and contents " << (hDest == UNDEF ? "deleted" : "moved") << endl;
                }
                else
                {
                    cout << " stopped" << endl;
                }
            }
            else
            {
                cout << "Backup Centre - Failed to " << (isBackup ? "remove Backup " : "stop sync ") << toHandle(backupId);
                if (isBackup)
                {
                    cout << " and " << (hDest == UNDEF ? "deleted" : "moved") << " its contents";
                }
                cout << " (" << errorstring(e) << ')' << endl;
            }
        });
    }

    else if ((pauseFlag || resumeFlag) && s.words.size() == 2) // pause/resume sync (any kind)
    {
        exec_backupcentreUpdateState(s.words[1].s,
                                     pauseFlag ? CommandBackupPut::TEMPORARY_DISABLED :
                                                 CommandBackupPut::ACTIVE);
    }
}
#endif

#ifdef MEGASDK_DEBUG_TEST_HOOKS_ENABLED
void exec_simulatecondition(autocomplete::ACState& s)
{
    auto condition = s.words[1].s;
    if (condition == "ETOOMANY")
    {
        globalMegaTestHooks.interceptSCRequest = [](std::unique_ptr<HttpReq>& pendingsc){
            pendingsc.reset(new HttpReq);
            pendingsc->status = REQ_SUCCESS;
            pendingsc->in = "-6";
            globalMegaTestHooks.interceptSCRequest = nullptr;
            cout << "ETOOMANY prepared and reset" << endl;
        };
        client->waiter->notify();
    }
    else
    {
        cout << "unknown condition: " << condition << endl;
    }
}
#endif

#ifdef ENABLE_SYNC
void exec_syncoutput(autocomplete::ACState& s)
{
    bool onOff = s.words[3].s == "on";

    if (s.words[2].s == "local_change_detection")
    {
        syncout_local_change_detection = onOff;
    }
    else if (s.words[2].s == "remote_change_detection")
    {
        syncout_remote_change_detection = onOff;
    }
    else if (s.words[2].s == "transfer_activity")
    {
        syncout_transfer_activity = onOff;
    }
    else if (s.words[2].s == "folder_sync_state")
    {
        syncout_transfer_activity = onOff;
    }
    else if (s.words[2].s == "detail_log")
    {
        client->syncs.mDetailedSyncLogging = onOff;
    }
    else if (s.words[2].s == "all")
    {
        syncout_local_change_detection = onOff;
        syncout_remote_change_detection = onOff;
        syncout_transfer_activity = onOff;
        syncout_transfer_activity = onOff;
        client->syncs.mDetailedSyncLogging = onOff;
    }
}
#endif

static void exec_fusedb(autocomplete::ACState& state)
{
    using namespace fuse;

    // Upgrade or downgrade?
    auto function = ([&]() {
        if (state.words[2].s == "downgrade")
            return &fuse::Service::downgrade;

        return &fuse::Service::upgrade;
    })();

    // What version are we moving to?
    auto version = ([&]() {
        std::istringstream istream(state.words[4].s);
        std::size_t result = 0;

        istream >> result;

        return result;
    })();

    // Try and downgrade / upgrade the specified database.
    auto path = localPathArg(state.words[3].s);
    auto result = (client->mFuseService.*function)(path, version);

    if (result != MOUNT_SUCCESS)
    {
        std::cerr << "Unable to "
                  << state.words[2].s
                  << " the database \""
                  << state.words[3].s
                  << "\" to version "
                  << state.words[4].s
                  << ": "
                  << toString(result)
                  << std::endl;

        return;
    }

    auto command = state.words[2].s;

    command.front() =
      static_cast<char>(std::toupper(command.front()));

    std::cout << command
              << "d database \""
              << state.words[3].s
              << "\" to version "
              << state.words[4].s
              << std::endl;
}

static bool isFullAccount(const std::string& message)
{
    if (client->loggedin() == FULLACCOUNT)
        return true;

    std::cerr << message << std::endl;

    return false;
}

static void exec_fuseflags(autocomplete::ACState& state)
{
    using std::chrono::seconds;
    using std::stoul;

    auto parseCacheFlags = [&](fuse::InodeCacheFlags& flags) {
        std::string ageThreshold;
        std::string interval;
        std::string maxSize;
        std::string sizeThreshold;

        state.extractflagparam("-cache-clean-age-threshold", ageThreshold);
        state.extractflagparam("-cache-clean-interval", interval);
        state.extractflagparam("-cache-clean-size-threshold", sizeThreshold);
        state.extractflagparam("-cache-max-size", maxSize);

        if (!ageThreshold.empty())
            flags.mCleanAgeThreshold = seconds(stoul(ageThreshold));

        if (!interval.empty())
            flags.mCleanInterval = seconds(stoul(interval));

        if (!maxSize.empty())
            flags.mMaxSize = stoul(maxSize);

        if (!sizeThreshold.empty())
            flags.mCleanSizeThreshold = stoul(sizeThreshold);
    }; // parseCacheFlags

    auto parseExecutorFlags =
      [&](fuse::TaskExecutorFlags& flags, const std::string& type) {
        std::string idle;
        std::string max;
        std::string min;

        state.extractflagparam("-" + type + "-max-thread-count", max);
        state.extractflagparam("-" + type + "-max-thread-idle-time", idle);
        state.extractflagparam("-" + type + "-min-thread-count", min);

        if (!idle.empty())
            flags.mIdleTime = seconds(stoul(idle));

        if (!max.empty())
            flags.mMaxWorkers = stoul(max);

        if (!min.empty())
            flags.mMinWorkers = stoul(min);
    }; // parseExecutorFlags

    std::string flushDelay;
    std::string logLevel;

    state.extractflagparam("-flush-delay", flushDelay);
    state.extractflagparam("-log-level", logLevel);

    auto flags = client->mFuseService.serviceFlags();

    if (!flushDelay.empty())
        flags.mFlushDelay = std::chrono::seconds(std::stoul(flushDelay));

    if (!logLevel.empty())
        flags.mLogLevel = fuse::toLogLevel(logLevel);

    parseCacheFlags(flags.mInodeCacheFlags);
    parseExecutorFlags(flags.mMountExecutorFlags, "mount");
    parseExecutorFlags(flags.mServiceExecutorFlags, "service");

    client->mFuseService.serviceFlags(flags);

    std::cout << "Cache Clean Age Threshold: "
              << flags.mInodeCacheFlags.mCleanAgeThreshold.count()
              << "\n"
              << "Cache Clean Interval: "
              << flags.mInodeCacheFlags.mCleanInterval.count()
              << "\n"
              << "Cache Clean Size Threshold: "
              << flags.mInodeCacheFlags.mCleanSizeThreshold
              << "\n"
              << "Cache Max Size: "
              << flags.mInodeCacheFlags.mMaxSize
              << "\n"
              << "Flush Delay: "
              << flags.mFlushDelay.count()
              << "s\n"
              << "Log Level: "
              << toString(flags.mLogLevel)
              << "\n"
              << "Mount Max Thread Count: "
              << flags.mMountExecutorFlags.mMaxWorkers
              << "\n"
              << "Mount Max Thread Idle Time: "
              << flags.mMountExecutorFlags.mIdleTime.count()
              << "s\n"
              << "Mount Min Thread Count: "
              << flags.mMountExecutorFlags.mMinWorkers
              << "\n"
              << "Service Max Thread Count: "
              << flags.mServiceExecutorFlags.mMaxWorkers
              << "\n"
              << "Service Max Thread Idle Time: "
              << flags.mServiceExecutorFlags.mIdleTime.count()
              << "s\n"
              << "Service Min Thread Count: "
              << flags.mServiceExecutorFlags.mMinWorkers
              << std::endl;
}

static void exec_fusemountadd(autocomplete::ACState& state)
{
    if (!isFullAccount("You must be logged in to add a FUSE mount."))
        return;

    using namespace fuse;

    MountInfo info;

    state.extractflagparam("-name", info.mFlags.mName);

    info.mFlags.mPersistent = state.extractflag("-persistent");
    info.mFlags.mReadOnly = state.extractflag("-read-only");

    const auto sourcePath = state.words[3].s;
    const auto sourceNode = nodebypath(sourcePath.c_str());

    if (!sourceNode)
    {
        std::cerr << "Unable to add a mount against \""
                  << sourcePath
                  << "\" as the node does not exist."
                  << std::endl;
        return;
    }

    if (info.mFlags.mName.empty())
    {
        info.mFlags.mName = sourceNode->displayname();

        if (!sourceNode->parent)
            info.mFlags.mName = "MEGA";
    }

    auto targetPath = state.words[4].s;

    info.mHandle = sourceNode->nodeHandle();
    info.mPath = localPathArg(targetPath);

    auto result = client->mFuseService.add(info);

    if (result != MOUNT_SUCCESS)
    {
        std::cerr << "Failed to add mount against \""
                  << sourcePath
                  << "\" at \""
                  << targetPath
                  << "\": "
                  << toString(result)
                  << std::endl;

        return;
    }

    std::cout << "Successfully added mount against \""
              << sourcePath
              << "\" at \""
              << targetPath
              << "\"."
              << std::endl;
}

static void exec_fusemountdisable(autocomplete::ACState& state)
{
    if (!isFullAccount("You must be logged in to disable FUSE mounts."))
        return;

    using namespace fuse;

    auto name = std::string();
    auto path = std::string();

    if (state.extractflagparam("-name", name))
    {
        auto paths = client->mFuseService.paths(name);

        if (paths.size() > 1)
        {
            std::cerr << "Multiple mounts are associated with the name \""
                      << name
                      << "\"."
                      << std::endl;

            return;
        }

        if (paths.empty())
        {
            std::cerr << "There are no mounts named \""
                      << name
                      << "\"."
                      << std::endl;

            return;
        }

        path = paths.front().toPath(false);
    }

    state.extractflagparam("-path", path);

    auto callback = [=](MountResult result) {
        if (result == MOUNT_SUCCESS)
        {
            std::cout << "Successfully disabled mount \""
                      << path
                      << "\"."
                      << std::endl;
            return;
        }

        std::cerr << "Failed to disable mount \""
                  << path
                  << "\": "
                  << toString(result)
                  << std::endl;
    };

    auto remember = state.extractflag("-remember");

    client->mFuseService.disable(std::move(callback),
                                 localPathArg(path),
                                 remember);
}

static void exec_fusemountenable(autocomplete::ACState& state)
{
    if (!isFullAccount("You must be logged in to enable FUSE mounts."))
        return;

    using fuse::MOUNT_SUCCESS;

    auto name = std::string();
    auto path = std::string();

    if (state.extractflagparam("-name", name))
    {
        auto paths = client->mFuseService.paths(name);

        if (paths.size() > 1)
        {
            std::cerr << "Multiple mounts are associated with the name \""
                      << name
                      << "\"."
                      << std::endl;

            return;
        }

        if (paths.empty())
        {
            std::cerr << "There are no mounts named \""
                      << name
                      << "\"."
                      << std::endl;

            return;
        }

        path = paths.front().toPath(false);
    }

    state.extractflagparam("-path", path);

    auto remember = state.extractflag("-remember");

    auto result = client->mFuseService.enable(localPathArg(path), remember);

    if (result == MOUNT_SUCCESS)
    {
        std::cout << "Successfully enabled mount at \""
                  << path
                  << "\"."
                  << std::endl;

        return;
    }

    std::cerr << "Failed to enable mount at \""
              << path
              << "\": "
              << toString(result)
              << std::endl;
}

static void exec_fusemountflags(autocomplete::ACState& state)
{
    if (!isFullAccount("You must be logged in to alter FUSE mount flags."))
        return;

    auto name = std::string();
    auto path = std::string();

    if (state.extractflagparam("-by-name", name))
    {
        auto paths = client->mFuseService.paths(name);

        if (paths.size() > 1)
        {
            std::cerr << "Multiple mounts are associated with the name \""
                      << name
                      << "\"."
                      << std::endl;

            return;
        }

        if (paths.empty())
        {
            std::cerr << "There are no mounts named \""
                      << name
                      << "\"."
                      << std::endl;

            return;
        }

        path = paths.front().toPath(false);
    }

    state.extractflagparam("-by-path", path);

    auto flags = client->mFuseService.flags(localPathArg(path));

    if (!flags)
    {
        std::cerr << "Couldn't retrieve flags for mount at \""
                  << path
                  << "\"."
                  << std::endl;

        return;
    }

    auto disabled = state.extractflag("-disabled-at-startup");
    auto enabled  = state.extractflag("-enabled-at-startup");

    if (disabled && enabled)
    {
        std::cerr << "A mount is either disabled or enabled at startup."
                  << std::endl;

        return;
    }

    flags->mEnableAtStartup |= enabled;
    flags->mEnableAtStartup &= !disabled;
    flags->mPersistent |= enabled || disabled;

    state.extractflagparam("-name", flags->mName);

    auto readOnly = state.extractflag("-read-only");
    auto writable = state.extractflag("-writable");

    if (readOnly && writable)
    {
        std::cerr << "A mount is either read-only or writable."
                  << std::endl;

        return;
    }

    flags->mReadOnly |= readOnly;
    flags->mReadOnly &= !writable;

    auto persistent = state.extractflag("-persistent");
    auto transient  = state.extractflag("-transient");

    if (persistent && transient)
    {
        std::cerr << "A mount is either persistent or transient."
                  << std::endl;

        return;
    }

    flags->mPersistent |= persistent;
    flags->mPersistent &= !transient;

    auto result = client->mFuseService.flags(localPathArg(path), *flags);

    if (result != fuse::MOUNT_SUCCESS)
    {
        std::cerr << "Unable to update mount flags: "
                  << toString(result)
                  << std::endl;

        return;
    }

    std::cout << "Enabled at startup: "
              << flags->mEnableAtStartup
              << "\n"
              << "Name: "
              << flags->mName
              << "\n"
              << "Persistent: "
              << flags->mPersistent
              << "\n"
              << "Read-Only: "
              << flags->mReadOnly
              << std::endl;
}

static void exec_fusemountlist(autocomplete::ACState& state)
{
    if (!isFullAccount("You must be logged in to list FUSE mounts."))
        return;

    auto active = state.extractflag("-only-active");
    auto mounts = client->mFuseService.get(active);

    if (mounts.empty())
    {
        std::cout << "There are no FUSE mounts."
                  << std::endl;
        return;
    }

    for (auto i = 0u; i < mounts.size(); ++i)
    {
        const auto& info = mounts[i];

        auto sourceNode = client->nodeByHandle(info.mHandle);
        std::string sourcePath = "N/A";

        if (sourceNode)
            sourcePath = sourceNode->displaypath();

        std::cout << "Mount #"
                  << (i + 1)
                  << ":\n"
                  << "  Enabled at Startup: "
                  << (info.mFlags.mEnableAtStartup ? "Yes" : "No")
                  << "\n"
                  << "  Enabled: "
                  << client->mFuseService.enabled(info.mPath)
                  << "\n"
                  << "  Name: \""
                  << info.mFlags.mName
                  << "\"\n"
                  << "  Read "
                  << (info.mFlags.mReadOnly ? "Only" : "Write")
                  << "\n"
                  << "  Source Handle: "
                  << toNodeHandle(info.mHandle)
                  << "\n"
                  << "  Source Path: "
                  << sourcePath
                  << "\n"
                  << "  Target Path: "
                  << info.mPath.toPath(true)
                  << "\n"
                  << std::endl;
    }

    std::cout << "Listed "
              << mounts.size()
              << " FUSE mount(s)."
              << std::endl;
}

static void exec_fusemountremove(autocomplete::ACState& state)
{
    if (!isFullAccount("You must be logged in to remove a FUSE mount."))
        return;

    using namespace fuse;

    auto name = std::string();
    auto path = std::string();

    if (state.extractflagparam("-name", name))
    {
        auto paths = client->mFuseService.paths(name);

        if (paths.size() > 1)
        {
            std::cerr << "Multiple mounts are associated with the name \""
                      << name
                      << "\"."
                      << std::endl;

            return;
        }

        if (paths.empty())
        {
            std::cerr << "There are no mounts named \""
                      << name
                      << "\"."
                      << std::endl;

            return;
        }

        path = paths.front().toPath(false);
    }

    state.extractflagparam("-path", path);

    auto result = client->mFuseService.remove(localPathArg(path));

    if (result == MOUNT_SUCCESS)
    {
        std::cout << "Successfully removed mount against \""
                  << path
                  << "\"."
                  << std::endl;

        return;
    }

    std::cerr << "Failed to remove mount against \""
              << path
              << "\": "
              << toString(result)
              << std::endl;
}

MegaCLILogger gLogger;

autocomplete::ACN autocompleteSyntax()
{
    using namespace autocomplete;
    std::unique_ptr<Either> p(new Either("      "));

    p->Add(exec_apiurl, sequence(text("apiurl"), opt(sequence(param("url"), opt(param("disablepkp"))))));
    p->Add(exec_login, sequence(text("login"), opt(flag("-fresh")), either(sequence(param("email"), opt(param("password"))),
                                                      sequence(exportedLink(false, true), opt(param("auth_key"))),
                                                      param("session"),
                                                      sequence(text("autoresume"), opt(param("id"))))));
    p->Add(exec_begin, sequence(text("begin"), opt(flag("-e++")),
                                opt(either(sequence(param("firstname"), param("lastname")),     // to create an ephemeral++
                                        param("ephemeralhandle#ephemeralpw"),               // to resume an ephemeral
                                        param("session")))));                                 // to resume an ephemeral++
    p->Add(exec_signup, sequence(text("signup"),
                                 either(sequence(param("email"), param("name")),
                                        param("confirmationlink"))));

    p->Add(exec_cancelsignup, sequence(text("cancelsignup")));
    p->Add(exec_session, sequence(text("session"), opt(sequence(text("autoresume"), opt(param("id"))))));
    p->Add(exec_mount, sequence(text("mount")));
    p->Add(exec_ls, sequence(text("ls"), opt(flag("-R")), opt(sequence(flag("-tofile"), param("filename"))), opt(remoteFSFolder(client, &cwd))));
    p->Add(exec_cd, sequence(text("cd"), opt(remoteFSFolder(client, &cwd))));
    p->Add(exec_pwd, sequence(text("pwd")));
    p->Add(exec_lcd, sequence(text("lcd"), opt(localFSFolder())));
    p->Add(exec_llockfile, sequence(text("llockfile"), opt(flag("-read")), opt(flag("-write")), opt(flag("-unlock")), localFSFile()));
#ifdef USE_FILESYSTEM
    p->Add(exec_lls, sequence(text("lls"), opt(flag("-R")), opt(localFSFolder())));
    p->Add(exec_lpwd, sequence(text("lpwd")));
    p->Add(exec_lmkdir, sequence(text("lmkdir"), localFSFolder()));
#endif
    p->Add(exec_import, sequence(text("import"), exportedLink(true, false)));
    p->Add(exec_folderlinkinfo, sequence(text("folderlink"), opt(param("link"))));

    p->Add(exec_open,
           sequence(text("open"),
                    exportedLink(false, true),
                    opt(param("authToken"))));

    p->Add(exec_put, sequence(text("put"), opt(flag("-r")), opt(flag("-noversion")), opt(flag("-version")), opt(flag("-versionreplace")), opt(flag("-allowduplicateversions")), localFSPath("localpattern"), opt(either(remoteFSPath(client, &cwd, "dst"),param("dstemail")))));
    p->Add(exec_putq, sequence(text("putq"), repeat(either(flag("-active"), flag("-all"), flag("-count"))), opt(param("cancelslot"))));
#ifdef USE_FILESYSTEM
    p->Add(exec_get, sequence(text("get"), opt(sequence(flag("-r"), opt(flag("-foldersonly")))), remoteFSPath(client, &cwd), opt(sequence(param("offset"), opt(param("length"))))));
#else
    p->Add(exec_get, sequence(text("get"), remoteFSPath(client, &cwd), opt(sequence(param("offset"), opt(param("length"))))));
#endif
    p->Add(exec_get, sequence(text("get"), flag("-re"), param("regularexpression")));
    p->Add(exec_get, sequence(text("get"), exportedLink(true, false), opt(sequence(param("offset"), opt(param("length"))))));
    p->Add(exec_getq, sequence(text("getq"), repeat(either(flag("-active"), flag("-all"), flag("-count"))), opt(param("cancelslot"))));
    p->Add(exec_more, sequence(text("more"), opt(remoteFSPath(client, &cwd))));
    p->Add(exec_pause, sequence(text("pause"), either(text("status"), sequence(opt(either(text("get"), text("put"))), opt(text("hard"))))));
    p->Add(exec_getfa, sequence(text("getfa"), wholenumber(1), opt(remoteFSPath(client, &cwd)), opt(text("cancel"))));
#ifdef USE_MEDIAINFO
    p->Add(exec_mediainfo, sequence(text("mediainfo"), either(sequence(text("calc"), localFSFile()), sequence(text("show"), remoteFSFile(client, &cwd)))));
#endif
    p->Add(exec_smsverify, sequence(text("smsverify"), either(sequence(text("send"), param("phonenumber"), opt(param("reverifywhitelisted"))), sequence(text("code"), param("verificationcode")))));
    p->Add(exec_verifiedphonenumber, sequence(text("verifiedphone")));
    p->Add(exec_resetverifiedphonenumber, sequence(text("resetverifiedphone")));
    p->Add(exec_mkdir, sequence(text("mkdir"), opt(flag("-allowduplicate")), opt(flag("-exactleafname")), opt(flag("-writevault")), remoteFSFolder(client, &cwd)));
    p->Add(exec_rm, sequence(text("rm"), remoteFSPath(client, &cwd), opt(sequence(flag("-regexchild"), param("regex")))));
    p->Add(exec_mv, sequence(text("mv"), remoteFSPath(client, &cwd, "src"), remoteFSPath(client, &cwd, "dst")));
    p->Add(exec_cp, sequence(text("cp"), opt(flag("-noversion")), opt(flag("-version")), opt(flag("-versionreplace")), opt(flag("-allowduplicateversions")), remoteFSPath(client, &cwd, "src"), either(remoteFSPath(client, &cwd, "dst"), param("dstemail"))));
    p->Add(exec_du, sequence(text("du"), opt(flag("-listfolders")), opt(remoteFSPath(client, &cwd))));
    p->Add(exec_numberofnodes, sequence(text("nn")));
    p->Add(exec_numberofchildren, sequence(text("nc"), opt(remoteFSPath(client, &cwd))));
    p->Add(exec_searchbyname, sequence(text("sbn"), param("name"), opt(param("nodeHandle")), opt(flag("-norecursive")), opt(flag("-nosensitive"))));
    p->Add(exec_nodedescription,
           sequence(text("nodedescription"),
                    remoteFSPath(client, &cwd),
                    opt(either(flag("-remove"), sequence(flag("-set"), param("description"))))));
    p->Add(exec_nodesensitive,
           sequence(text("nodesensitive"), remoteFSPath(client, &cwd), opt(flag("-remove"))));
    p->Add(exec_nodeTag,
           sequence(text("nodetag"),
                    remoteFSPath(client, &cwd),
                    opt(either(sequence(flag("-remove"), param("tag")),
                               sequence(flag("-add"), param("tag")),
                               sequence(flag("-update"), param("newtag"), param("oldtag"))))));

#ifdef ENABLE_SYNC
    p->Add(exec_setdevicename, sequence(text("setdevicename"), param("device_name")));
    p->Add(exec_getdevicename, sequence(text("getdevicename")));
    p->Add(exec_setextdrivename, sequence(text("setextdrivename"), param("drive_path"), param("drive_name")));
    p->Add(exec_getextdrivename, sequence(text("getextdrivename"), opt(either(sequence(flag("-id"), param("b64driveid")), sequence(flag("-path"), param("drivepath"))))));
    p->Add(exec_setmybackups, sequence(text("setmybackups"), param("mybackup_folder")));
    p->Add(exec_getmybackups, sequence(text("getmybackups")));
    p->Add(exec_backupcentre, sequence(text("backupcentre"), opt(either(
                                       sequence(flag("-del"), param("backup_id"), opt(param("move_to_handle"))),
                                       sequence(flag("-purge")),
                                       sequence(either(flag("-stop"), flag("-pause"), flag("-resume")), param("backup_id"))))));

    p->Add(exec_syncadd,
           sequence(text("sync"),
                    text("add"),
                    opt(flag("-scan-only")),
                    opt(sequence(flag("-scan-interval"), param("interval-secs"))),
                    either(
                        sequence(flag("-backup"),
                            opt(sequence(flag("-external"), param("drivePath"))),
                            opt(sequence(flag("-name"), param("syncname"))),
                            localFSFolder("source")),
                        sequence(opt(sequence(flag("-name"), param("syncname"))),
                            localFSFolder("source"),
                            remoteFSFolder(client, &cwd, "target")))));

    p->Add(exec_syncrename,
           sequence(text("sync"),
                    text("rename"),
                    backupID(*client),
                    param("newname")));

    p->Add(exec_syncclosedrive,
           sequence(text("sync"),
                    text("closedrive"),
                    localFSFolder("drive")));

    p->Add(exec_syncexport,
           sequence(text("sync"),
                    text("export"),
                    opt(localFSFile("outputFile"))));

    p->Add(exec_syncimport,
           sequence(text("sync"),
                    text("import"),
                    localFSFile("inputFile")));

    p->Add(exec_syncopendrive,
           sequence(text("sync"),
                    text("opendrive"),
                    localFSFolder("drive")));

    p->Add(exec_synclist,
           sequence(text("sync"), text("list")));

    p->Add(exec_syncremove,
           sequence(text("sync"),
                    text("remove"),
                    either(backupID(*client),
                           sequence(flag("-by-local-path"),
                                    localFSFolder()),
                           sequence(flag("-by-remote-path"),
                                    remoteFSFolder(client, &cwd))),
                    opt(param("backupdestinationfolder"))));

    p->Add(exec_syncstatus,
           sequence(text("sync"),
                    text("status"),
                    opt(param("id"))));

    p->Add(exec_syncxable, sequence(text("sync"),
            either(text("run"), text("pause"), text("suspend"), text("disable")),
            opt(sequence(flag("-error"), param("errorID"))),
            param("id")));

    p->Add(exec_syncrescan, sequence(text("sync"), text("rescan"), param("id")));

    p->Add(exec_syncoutput,
           sequence(text("sync"),
                    text("output"),
                    either(text("local_change_detection"),
                           text("remote_change_detection"),
                           text("transfer_activity"),
                           text("folder_sync_state"),
                           text("detail_log"),
                           text("all")),
                    either(text("on"),
                           text("off"))));

#endif

    p->Add(exec_export, sequence(text("export"), remoteFSPath(client, &cwd), opt(flag("-mega-hosted")), opt(either(flag("-writable"), param("expiretime"), text("del")))));
    p->Add(exec_encryptLink, sequence(text("encryptlink"), param("link"), param("password")));
    p->Add(exec_decryptLink, sequence(text("decryptlink"), param("link"), param("password")));
    p->Add(exec_share, sequence(text("share"), opt(sequence(remoteFSPath(client, &cwd), opt(sequence(contactEmail(client), opt(either(text("r"), text("rw"), text("full"))), opt(param("origemail"))))))));
    p->Add(exec_invite, sequence(text("invite"), param("dstemail"), opt(either(param("origemail"), text("del"), text("rmd")))));

    p->Add(exec_clink, sequence(text("clink"), either(text("renew"), sequence(text("query"), param("handle")), sequence(text("del"), opt(param("handle"))))));

    p->Add(exec_ipc, sequence(text("ipc"), param("handle"), either(text("a"), text("d"), text("i"))));
    p->Add(exec_showpcr, sequence(text("showpcr")));
    p->Add(exec_users, sequence(text("users"), opt(sequence(contactEmail(client), text("del")))));
    p->Add(exec_getemail, sequence(text("getemail"), param("handle_b64")));
    p->Add(exec_getua, sequence(text("getua"), param("attrname"), opt(contactEmail(client))));
    p->Add(exec_putua, sequence(text("putua"), param("attrname"), opt(either(
                                                                          text("del"),
                                                                          sequence(text("set"), param("string")),
                                                                          sequence(text("map"), param("key"), param("value")),
                                                                          sequence(text("load"), localFSFile())))));
#ifdef DEBUG
    p->Add(exec_delua, sequence(text("delua"), param("attrname")));
    p->Add(exec_devcommand, sequence(text("devcommand"), param("subcommand"),
                                     opt(sequence(flag("-e"), param("email"))),
                                     opt(sequence(flag("-c"), param("campaign"),
                                                  flag("-g"), param("group_id")))));
#endif
#ifdef MEGASDK_DEBUG_TEST_HOOKS_ENABLED
    p->Add(exec_simulatecondition, sequence(text("simulatecondition"), opt(text("ETOOMANY"))));
#endif
    p->Add(exec_alerts, sequence(text("alerts"), opt(either(text("new"), text("old"), wholenumber(10), text("notify"), text("seen")))));
    p->Add(exec_recentactions,
           sequence(text("recentactions"),
                    param("hours"),
                    param("maxcount"),
                    opt(flag("-nosensitive"))));
    p->Add(exec_recentnodes, sequence(text("recentnodes"), param("hours"), param("maxcount")));

    p->Add(exec_putbps, sequence(text("putbps"), opt(either(wholenumber(100000), text("auto"), text("none")))));
    p->Add(exec_killsession, sequence(text("killsession"), either(text("all"), param("sessionid"))));
    p->Add(exec_whoami, sequence(text("whoami"), repeat(either(flag("-storage"), flag("-transfer"), flag("-pro"), flag("-transactions"), flag("-purchases"), flag("-sessions")))));
    p->Add(exec_verifycredentials, sequence(text("credentials"), either(text("show"), text("status"), text("verify"), text("reset")), opt(contactEmail(client))));
    p->Add(exec_manualverif, sequence(text("verification"), opt(either(flag("-on"), flag("-off")))));
    p->Add(exec_passwd, sequence(text("passwd")));
    p->Add(exec_reset, sequence(text("reset"), contactEmail(client), opt(text("mk"))));
    p->Add(exec_recover, sequence(text("recover"), param("recoverylink")));
    p->Add(exec_cancel, sequence(text("cancel"), opt(param("cancellink"))));
    p->Add(exec_email, sequence(text("email"), opt(either(param("newemail"), param("emaillink")))));
    p->Add(exec_retry, sequence(text("retry")));
    p->Add(exec_recon, sequence(text("recon")));
    p->Add(exec_reload, sequence(text("reload"), opt(text("nocache"))));
    p->Add(exec_logout, sequence(text("logout"), opt(flag("-keepsyncconfigs"))));
    p->Add(exec_locallogout, sequence(text("locallogout")));
    p->Add(exec_version, sequence(text("version")));
    p->Add(exec_debug, sequence(text("debug"),
                opt(either(flag("-on"), flag("-off"), flag("-verbose"))),
                opt(either(flag("-console"), flag("-noconsole"))),
                opt(either(flag("-nofile"), sequence(flag("-file"), localFSFile())))
                ));

#if defined(WIN32) && defined(NO_READLINE)
    p->Add(exec_clear, sequence(text("clear")));
    p->Add(exec_codepage, sequence(text("codepage"), opt(sequence(wholenumber(65001), opt(wholenumber(65001))))));
    p->Add(exec_log, sequence(text("log"), either(text("utf8"), text("utf16"), text("codepage")), localFSFile()));
#endif
    p->Add(exec_test, sequence(text("test"), opt(param("data"))));
    p->Add(exec_fingerprint, sequence(text("fingerprint"), localFSFile("localfile")));
#ifdef ENABLE_CHAT
    p->Add(exec_chats, sequence(text("chats"), opt(param("chatid"))));
    p->Add(exec_chatc, sequence(text("chatc"), param("group"), repeat(opt(sequence(contactEmail(client), either(text("ro"), text("sta"), text("mod")))))));
    p->Add(exec_chati, sequence(text("chati"), param("chatid"), contactEmail(client), either(text("ro"), text("sta"), text("mod"))));
    p->Add(exec_chatcp, sequence(text("chatcp"), flag("-meeting"), param("mownkey"), opt(sequence(text("t"), param("title64"))),
                                 repeat(sequence(contactEmail(client), either(text("ro"), text("sta"), text("mod"))))));
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
    p->Add(exec_setmaxdownloadspeed, sequence(text("setmaxdownloadspeed"), opt(wholenumber(10000))));
    p->Add(exec_setmaxuploadspeed, sequence(text("setmaxuploadspeed"), opt(wholenumber(10000))));
    p->Add(exec_setmaxloglinesize, sequence(text("setmaxloglinesize"), wholenumber(10000)));
    p->Add(exec_handles, sequence(text("handles"), opt(either(text("on"), text("off")))));
    p->Add(exec_httpsonly, sequence(text("httpsonly"), opt(either(text("on"), text("off")))));
    p->Add(exec_showattrs, sequence(text("showattrs"), opt(either(text("on"), text("off")))));
    p->Add(exec_timelocal, sequence(text("mtimelocal"), either(text("set"), text("get")), localFSPath(), opt(param("datetime"))));

    p->Add(exec_mfac, sequence(text("mfac"), param("email")));
    p->Add(exec_mfae, sequence(text("mfae")));
    p->Add(exec_mfad, sequence(text("mfad"), param("pin")));

#if defined(WIN32) && defined(NO_READLINE)
    p->Add(exec_autocomplete, sequence(text("autocomplete"), opt(either(text("unix"), text("dos")))));
    p->Add(exec_history, sequence(text("history")));
#elif !defined(NO_READLINE)
    p->Add(exec_history,
           sequence(text("history"),
                    either(text("clear"),
                           text("list"),
                           sequence(either(text("read"),
                                           text("record"),
                                           text("write")),
                                    localFSFile("history")))));
#endif
    p->Add(exec_help, either(text("help"), text("h"), text("?")));
    p->Add(exec_quit, either(text("quit"), text("q"), text("exit")));

    p->Add(exec_find, sequence(text("find"), text("raided")));
    p->Add(exec_findemptysubfoldertrees, sequence(text("findemptysubfoldertrees"), opt(flag("-movetotrash"))));

#ifdef MEGA_MEASURE_CODE
    p->Add(exec_deferRequests, sequence(text("deferrequests"), repeat(either(flag("-putnodes")))));
    p->Add(exec_sendDeferred, sequence(text("senddeferred"), opt(flag("-reset"))));
    p->Add(exec_codeTimings, sequence(text("codetimings"), opt(flag("-reset"))));
#endif

#ifdef USE_FILESYSTEM
    p->Add(exec_treecompare, sequence(text("treecompare"), localFSPath(), remoteFSPath(client, &cwd)));
    p->Add(exec_generatetestfilesfolders, sequence(text("generatetestfilesfolders"),
        repeat(either(  sequence(flag("-folderdepth"), param("depth")),
                        sequence(flag("-folderwidth"), param("width")),
                        sequence(flag("-filecount"), param("count")),
                        sequence(flag("-filesize"), param("size")),
                        sequence(flag("-nameprefix"), param("prefix")))), localFSFolder("parent")));
    p->Add(exec_generatesparsefile, sequence(text("generatesparsefile"), opt(sequence(flag("-filesize"), param("size"))), localFSFile("targetfile")));
    p->Add(exec_generate_put_fileversions, sequence(text("generate_put_fileversions"), opt(sequence(flag("-count"), param("n"))), localFSFile("targetfile")));
    p->Add(exec_lreplace, sequence(text("lreplace"), either(flag("-file"), flag("-folder")), localFSPath("existing"), param("content")));
    p->Add(exec_lrenamereplace, sequence(text("lrenamereplace"), either(flag("-file"), flag("-folder")), localFSPath("existing"), param("content"), localFSPath("renamed")));

    p->Add(exec_cycleUploadDownload, sequence(text("cycleuploaddownload"),
        repeat(either(
            sequence(flag("-filecount"), param("count")),
            sequence(flag("-filesize"), param("size")),
            sequence(flag("-nameprefix"), param("prefix")))), localFSFolder("localworkingfolder"), remoteFSFolder(client, &cwd, "remoteworkingfolder")));

#endif
    p->Add(exec_querytransferquota, sequence(text("querytransferquota"), param("filesize")));
    p->Add(exec_getcloudstorageused, sequence(text("getcloudstorageused")));
    p->Add(exec_getuserquota, sequence(text("getuserquota"), repeat(either(flag("-storage"), flag("-transfer"), flag("-pro")))));
    p->Add(exec_getuserdata, text("getuserdata"));

    p->Add(exec_showattributes, sequence(text("showattributes"), remoteFSPath(client, &cwd)));

    p->Add(exec_setmaxconnections, sequence(text("setmaxconnections"), either(text("put"), text("get")), opt(wholenumber(4))));
    p->Add(exec_metamac, sequence(text("metamac"), localFSPath(), remoteFSPath(client, &cwd)));
    p->Add(exec_banner, sequence(text("banner"), either(text("get"), sequence(text("dismiss"), param("id")))));

    p->Add(exec_drivemonitor, sequence(text("drivemonitor"), opt(either(flag("-on"), flag("-off")))));

    p->Add(exec_driveid,
           sequence(text("driveid"),
                    either(sequence(text("get"), localFSFolder()),
                           sequence(text("set"), localFSFolder(), opt(text("force"))))));

    p->Add(exec_randomfile,
           sequence(text("randomfile"),
                    localFSPath("outputPath"),
                    opt(param("lengthKB"))));

    p->Add(exec_setsandelements,
        sequence(text("setsandelements"),
                 either(text("list"),
                        sequence(text("newset"), param("type"), opt(param("name"))),
                        sequence(text("updateset"), param("id"), opt(sequence(flag("-n"), opt(param("name")))), opt(sequence(flag("-c"), opt(param("cover"))))),
                        sequence(text("removeset"), param("id")),
                        sequence(text("newelement"), param("setid"), param("nodehandle"),
                                 opt(sequence(flag("-n"), param("name"))), opt(sequence(flag("-o"), param("order")))),
                        sequence(text("updateelement"), param("sid"), param("eid"),
                                 opt(sequence(flag("-n"), opt(param("name")))), opt(sequence(flag("-o"), param("order")))),
                        sequence(text("removeelement"), param("sid"), param("eid")),
                        sequence(text("export"), param("sid"), opt(flag("-disable"))),
                        sequence(text("getpubliclink"), param("sid")),
                        sequence(text("fetchpublicset"), param("publicsetlink")),
                        text("getsetinpreview"),
                        text("stoppublicsetpreview"),
                        sequence(text("downloadelement"), param("sid"), param("eid"))
                        )));

    p->Add(exec_reqstat, sequence(text("reqstat"), opt(either(flag("-on"), flag("-off")))));
    p->Add(exec_getABTestValue, sequence(text("getabflag"), param("flag")));
    p->Add(exec_sendABTestActive, sequence(text("setabflag"), param("flag")));
    p->Add(exec_contactVerificationWarning, sequence(text("verificationwarnings"), opt(either(flag("-on"), flag("-off")))));

    /* MEGA VPN commands */
    p->Add(exec_getvpnregions, text("getvpnregions"));
    p->Add(exec_getvpncredentials, sequence(text("getvpncredentials"), opt(sequence(flag("-s"), param("slotID"))), opt(flag("-noregions"))));
    p->Add(exec_putvpncredential, sequence(text("putvpncredential"), param("region"), opt(sequence(flag("-file"), param("credentialfilewithoutextension"))), opt(flag("-noconsole"))));
    p->Add(exec_delvpncredential, sequence(text("delvpncredential"), param("slotID")));
    p->Add(exec_checkvpncredential, sequence(text("checkvpncredential"), param("userpublickey")));
    /* MEGA VPN commands END */

    p->Add(exec_fetchcreditcardinfo, text("cci"));

    p->Add(exec_passwordmanager,
           sequence(text("pwdman"),
                    either(text("list"),
                           text("getbase"),
                           text("createbase"),
                           text("removebase"),
                           sequence(text("newfolder"), param("parenthandle"), param("name")),
                           sequence(text("renamefolder"), param("handle"), param("name")),
                           sequence(text("removefolder"), param("handle")),
                           sequence(text("newentry"),
                                    param("parenthandle"),
                                    param("name"),
                                    param("pwd"),
                                    opt(sequence(flag("-url"), param("url"))),
                                    opt(sequence(flag("-u"), param("username"))),
                                    opt(sequence(flag("-n"), param("notes")))),
                           sequence(text("newentries"),
                                    param("parenthandle"),
                                    repeat(sequence(param("name"), param("uname"), param("pwd")))),
                           sequence(text("getentrydata"), param("nodehandle")),
                           sequence(text("renameentry"), param("nodehandle"), param("name")),
                           sequence(text("updateentry"),
                                    param("nodehandle"),
                                    opt(sequence(flag("-p"), param("pwd"))),
                                    opt(sequence(flag("-url"), param("url"))),
                                    opt(sequence(flag("-u"), param("username"))),
                                    opt(sequence(flag("-n"), param("note")))),
                           sequence(text("removeentry"), param("nodehandle")))));

    p->Add(exec_generatepassword,
           sequence(text("generatepassword"),
                    either(sequence(text("chars"),
                                    param("length"),
                                    opt(flag("-useUpper")),
                                    opt(flag("-useDigits")),
                                    opt(flag("-useSymbols")))
                        )));

    p->Add(exec_importpasswordsfromgooglefile,
           sequence(text("importpasswordsgoogle"), localFSPath("file"), param("parenthandle")));

    p->Add(exec_fusedb,
           sequence(text("fuse"),
                    text("db"),
                    either(text("downgrade"),
                           text("upgrade")),
                    localFSFile("database"),
                    wholenumber(0)));

    p->Add(exec_fuseflags,
           sequence(text("fuse"),
                    text("flags"),
                    repeat(either(sequence(flag("-cache-clean-age-threshold"),
                                           wholenumber("seconds", 5 * 60)),
                                  sequence(flag("-cache-clean-interval"),
                                           wholenumber("seconds", 5 * 60)),
                                  sequence(flag("-cache-clean-size-threshold"),
                                           wholenumber("count", 64)),
                                  sequence(flag("-cache-max-size"),
                                           wholenumber("count", 256)),
                                  sequence(flag("-flush-delay"),
                                           wholenumber("seconds", 4)),
                                  sequence(flag("-log-level"),
                                           either(text("DEBUG"),
                                                  text("ERROR"),
                                                  text("INFO"),
                                                  text("WARNING"))),
                                  sequence(flag("-mount-max-thread-count"),
                                           wholenumber("count", 16)),
                                  sequence(flag("-mount-max-thread-idle-time"),
                                           wholenumber("seconds", 16)),
                                  sequence(flag("-mount-min-thread-count"),
                                           wholenumber("count", 0)),
                                  sequence(flag("-service-max-thread-count"),
                                           wholenumber("count", 16)),
                                  sequence(flag("-service-max-thread-idle-time"),
                                           wholenumber("seconds", 16)),
                                  sequence(flag("-service-min-thread-count"),
                                           wholenumber("count", 0))))));

    p->Add(exec_fusemountadd,
           sequence(text("fuse"),
                    text("mount"),
                    text("add"),
                    repeat(either(sequence(flag("-name"),
                                           param("name")),
                                  flag("-persistent"),
                                  flag("-read-only"))),
                    remoteFSFolder(client, &cwd, "source"),
                    localFSFolder("target")));

    p->Add(exec_fusemountdisable,
           sequence(text("fuse"),
                    text("mount"),
                    text("disable"),
                    sequence(either(sequence(flag("-name"),
                                             param("name")),
                                    sequence(flag("-path"),
                                             localFSFolder("target"))),
                             opt(flag("-remember")))));

    p->Add(exec_fusemountenable,
           sequence(text("fuse"),
                    text("mount"),
                    text("enable"),
                    sequence(either(sequence(flag("-name"),
                                             param("name")),
                                    sequence(flag("-path"),
                                             localFSFolder("target"))),
                             opt(flag("-remember")))));

    p->Add(exec_fusemountflags,
           sequence(text("fuse"),
                    text("mount"),
                    text("flags"),
                    either(sequence(flag("-by-name"),
                                    param("name")),
                           sequence(flag("-by-path"),
                                    localFSFolder("target"))),
                    repeat(either(flag("-disabled-at-startup"),
                                  flag("-enabled-at-startup"),
                                  sequence(flag("-name"),
                                           param("name")),
                                  flag("-persistent"),
                                  flag("-read-only"),
                                  flag("-transient"),
                                  flag("-writable")))));

    p->Add(exec_fusemountlist,
           sequence(text("fuse"),
                    text("mount"),
                    text("list"),
                    opt(flag("-only-active"))));

    p->Add(exec_fusemountremove,
           sequence(text("fuse"),
                    text("mount"),
                    text("remove"),
                    either(sequence(flag("-name"),
                                    param("name")),
                           sequence(flag("-path"),
                                    localFSFolder("target")))));

    p->Add(exec_getpricing, text("getpricing"));

    p->Add(exec_collectAndPrintTransferStats,
           sequence(text("getTransferStats"), opt(either(flag("-uploads"), flag("-downloads")))));

    p->Add(exec_hashcash, sequence(text("hashcash"), opt(either(flag("-on"), flag("-off")))));
    return autocompleteTemplate = std::move(p);
}


#ifdef USE_FILESYSTEM
bool recursiveget(fs::path&& localpath, Node* n, bool folders, unsigned& queued)
{
    if (n->type == FILENODE)
    {
        if (!folders)
        {
            TransferDbCommitter committer(client->tctable);
            auto file = std::make_unique<AppFileGet>(n, NodeHandle(), nullptr, -1, 0, nullptr, nullptr, localpath.u8string());
            error result = startxfer(committer, std::move(file), *n, client->nextreqtag());
            queued += result == API_OK ? 1 : 0;
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
        for (auto& node : client->getChildren(n))
        {
            if (!recursiveget(std::move(newpath), node.get(), folders, queued))
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
            TransferDbCommitter committer(client->tctable);
            for (auto& node : client->getChildren(n))
            {
                if (node->type == FILENODE)
                {
                    if (regex_search(string(node->displayname()), re))
                    {
                        auto file = std::make_unique<AppFileGet>(node.get());
                        error result = startxfer(committer, std::move(file), *node, client->nextreqtag());
                        queued += result == API_OK ? 1 : 0;
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
    bool succeeded = false;

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
                mc->saveV1Pwd(password.c_str()); // for automatic upgrade to V2
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

    void fetchnodes(MegaClient* mc)
    {
        assert(succeeded);
        cout << "Retrieving account after a succesful login..." << endl;
        mc->fetchnodes(false, true, false);
        succeeded = false;
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

            signupcode.clear();
        }
        else if (recoverycode.size())   // cancelling account --> check password
        {
            client->pw_key(l, pwkey);
            client->validatepwd(l);
        }
        else if (changecode.size())     // changing email --> check password to avoid creating an invalid hash
        {
            client->pw_key(l, pwkey);
            client->validatepwd(l);
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
            if (signupemail.size())
            {
                string buf = client->sendsignuplink2(signupemail.c_str(), newpassword.c_str(), signupname.c_str());
                cout << endl <<  "Updating derived key of ephemeral session, session ID: ";
                cout << Base64Str<MegaClient::USERHANDLE>(client->me) << "#";
                cout << Base64Str<SymmCipher::KEYLENGTH>((const byte*)buf.data()) << endl;
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
                if (client->changepw(newpassword.c_str()) == API_OK)
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
    case PAGER:
        if (strlen(l) && l[0] == 'q')
        {
            setprompt(COMMAND); // quit pager view if 'q' is sent, see README
        }
        else
        {
            autocomplete::ACState nullState; //not entirely sure about this
            exec_more(nullState); //else, get one more page
        }
        return;
    }
}

void exec_ls(autocomplete::ACState& s)
{
    std::shared_ptr<Node> n;
    bool recursive = s.extractflag("-R");
    string toFilename;
    bool toFileFlag = s.extractflagparam("-tofile", toFilename);

    ofstream toFile;
    if (toFileFlag)
    {
        toFile.open(toFilename);
    }

    if (s.words.size() > 1)
    {
        n = nodebypath(s.words[1].s.c_str());
    }
    else
    {
        n = client->nodeByHandle(cwd);
    }

    if (n)
    {
        dumptree(n.get(), recursive, 0, NULL, toFileFlag ? &toFile : nullptr);
    }
}

void exec_cd(autocomplete::ACState& s)
{
    if (s.words.size() > 1)
    {
        if (std::shared_ptr<Node> n = nodebypath(s.words[1].s.c_str()))
        {
            if (n->type == FILENODE)
            {
                cout << s.words[1].s << ": Not a directory" << endl;
            }
            else
            {
                cwd = n->nodeHandle();
            }
        }
        else
        {
            cout << s.words[1].s << ": No such file or directory" << endl;
        }
    }
    else
    {
        cwd = client->mNodeManager.getRootNodeFiles();
    }
}

void exec_rm(autocomplete::ACState& s)
{
    string childregexstring;
    bool useregex = s.extractflagparam("-regexchild", childregexstring);

    if (std::shared_ptr<Node> n = nodebypath(s.words[1].s.c_str()))
    {
        vector<std::shared_ptr<Node> > v;
        sharedNode_list children;
        if (useregex)
        {
            std::regex re(childregexstring);
            children = client->getChildren(n.get());
            for (auto& c : children)
            {
                if (std::regex_match(c->displayname(), re))
                {
                    v.push_back(c);
                }
            }
        }
        else
        {
            v.push_back(n);
        }

        for (auto& d : v)
        {
            error e = client->unlink(d.get(), false, 0, false);

            if (e)
            {
                cout << d->displaypath() << ": Deletion failed (" << errorstring(e) << ")" << endl;
            }
        }
    }
    else
    {
        cout << s.words[1].s << ": No such file or directory" << endl;
    }
}

void exec_mv(autocomplete::ACState& s)
{
    std::shared_ptr<Node> n, tn;
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
                        if ((e = client->checkmove(n.get(), tn.get())) == API_OK)
                        {
                            if (!client->checkaccess(n.get(), RDWR))
                            {
                                cout << "Write access denied" << endl;

                                return;
                            }

                            // rename
                            LocalPath::utf8_normalize(&newname);

                            if ((e = client->setattr(n, attr_map('n', newname), setattr_result, false)))
                            {
                                cout << "Cannot rename file (" << errorstring(e) << ")" << endl;
                            }
                        }
                        else
                        {
                            cout << "Cannot rename file (" << errorstring(e) << ")" << endl;
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

                        if ((e = client->checkmove(n.get(), tn->parent.get())) == API_OK)
                        {
                            if (!client->checkaccess(n.get(), RDWR))
                            {
                                cout << "Write access denied" << endl;

                                return;
                            }

                            // overwrite existing target file: rename source...
                            e = client->setattr(n, attr_map('n', tn->attrs.map['n']), setattr_result, false);

                            if (e)
                            {
                                cout << "Rename failed (" << errorstring(e) << ")" << endl;
                            }

                            if (n != tn)
                            {
                                // ...delete target...
                                e = client->unlink(tn.get(), false, 0, false);

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
                        e = client->checkmove(n.get(), tn.get());
                    }
                }

                if (n->parent != tn)
                {
                    if (e == API_OK)
                    {
                        e = client->rename(n, tn, SYNCDEL_NONE, NodeHandle(), nullptr, false, rename_result);

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
    std::shared_ptr<Node> n, tn;
    string targetuser;
    string newname;
    error e;


    VersioningOption vo = UseLocalVersioningFlag;
    if (s.extractflag("-noversion")) vo = NoVersioning;
    if (s.extractflag("-version")) vo = ClaimOldVersion;
    if (s.extractflag("-versionreplace")) vo = ReplaceOldVersion;

    bool allowDuplicateVersions = s.extractflag("-allowduplicateversions");

    if (s.words.size() > 2)
    {
        if ((n = nodebypath(s.words[1].s.c_str())))
        {
            if ((tn = nodebypath(s.words[2].s.c_str(), &targetuser, &newname)))
            {
                if (!client->checkaccess(tn.get(), RDWR))
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
                        e = client->unlink(tn.get(), false, 0, false);

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
            NodeHandle ovhandle;

            if (!n->keyApplied())
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
                LocalPath::utf8_normalize(&sname);
            }
            else
            {
                attr_map::iterator it = n->attrs.map.find('n');
                if (it != n->attrs.map.end())
                {
                    sname = it->second;
                }
            }

            if (tn && n->type == FILENODE && !allowDuplicateVersions)
            {
                std::shared_ptr<Node> ovn = client->childnodebyname(tn.get(), sname.c_str(), true);
                if (ovn)
                {
                    if (n->isvalid && ovn->isvalid && *(FileFingerprint*)n.get() == *(FileFingerprint*)ovn.get())
                    {
                        cout << "Skipping identical node" << endl;
                        return;
                    }

                    ovhandle = ovn->nodeHandle();
                }
            }

            // determine number of nodes to be copied
            client->proctree(n, &tc, false, !ovhandle.isUndef());

            tc.allocnodes();

            // build new nodes array
            client->proctree(n, &tc, false, !ovhandle.isUndef());

            // if specified target is a filename, use it
            if (newname.size())
            {
                SymmCipher key;
                string attrstring;

                // copy source attributes and rename
                AttrMap attrs;

                attrs.map = n->attrs.map;
                attrs.map['n'] = sname;

                key.setkey((const byte*)tc.nn[0].nodekey.data(), tc.nn[0].type);

                // JSON-encode object and encrypt attribute string
                attrs.getjson(&attrstring);
                tc.nn[0].attrstring.reset(new string);
                client->makeattr(&key, tc.nn[0].attrstring, attrstring.c_str());
            }

            // tree root: no parent
            tc.nn[0].parenthandle = UNDEF;
            tc.nn[0].ovhandle = ovhandle;

            if (tn)
            {
                // add the new nodes
                client->putnodes(tn->nodeHandle(), vo, std::move(tc.nn), nullptr, gNextClientTag++, false);
            }
            else
            {
                if (targetuser.size())
                {
                    cout << "Attempting to drop into user " << targetuser << "'s inbox..." << endl;

                    client->putnodes(targetuser.c_str(), std::move(tc.nn), gNextClientTag++);
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

void exec_du(autocomplete::ACState &s)
{
    bool listfolders = s.extractflag("-listfolders");

    std::shared_ptr<Node> n;

    if (s.words.size() > 1)
    {
        n = nodebypath(s.words[1].s.c_str());
        if (!n)
        {
            cout << s.words[1].s << ": No such file or directory" << endl;
            return;
        }
    }
    else
    {
        n = client->nodeByHandle(cwd);
        if (!n)
        {
            cout << "cwd not set" << endl;
            return;
        }
    }

    if (listfolders)
    {
        auto list = client->getChildren(n.get());
        vector<shared_ptr<Node> > vec(list.begin(), list.end());
        std::sort(vec.begin(), vec.end(), [](shared_ptr<Node> & a, shared_ptr<Node> & b){
            return a->getCounter().files + a->getCounter().folders <
                   b->getCounter().files + b->getCounter().folders; });
        for (auto& f : vec)
        {
            if (f->type == FOLDERNODE)
            {
                NodeCounter nc = f->getCounter();
                cout << "folders:" << nc.folders << " files: " << nc.files << " versions: " << nc.versions << " storage: " << (nc.storage + nc.versionStorage) << " " << f->displayname() << endl;
            }
        }
    }
    else
    {
        NodeCounter nc = n->getCounter();

        cout << "Total storage used: " << nc.storage << endl;
        cout << "Total storage used by versions: " << nc.versionStorage << endl << endl;

        cout << "Total # of files: " << nc.files << endl;
        cout << "Total # of folders: " << nc.folders << endl;
        cout << "Total # of versions: " << nc.versions << endl;
    }
}

void exec_get(autocomplete::ACState& s)
{
    std::shared_ptr<Node> n;
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
            if (recursiveget(fs::current_path(), n.get(), true, queued))
            {
                if (!foldersonly)
                {
                    cout << "queueing files..." << endl;
                    bool alldone = recursiveget(fs::current_path(), n.get(), false, queued);
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
            if (regexget(regularexpression, n.get(), queued))
            {
                cout << "queued " << queued << " files for download" << endl;
            }
        }
    }
    else
    {
        handle ph = UNDEF;
        byte key[FILENODEKEYLENGTH];
        if (client->parsepubliclink(s.words[1].s.c_str(), ph, key, TypeOfLink::FILE) == API_OK)
        {
            cout << "Checking link..." << endl;

            client->reqs.add(new CommandGetFile(
                client,
                key,
                FILENODEKEYLENGTH,
                false,
                ph,
                false,
                nullptr,
                nullptr,
                nullptr,
                false,
                [key, ph](const Error& e,
                          m_off_t size,
                          dstime /*timeleft*/,
                          std::string* filename,
                          std::string* fingerprint,
                          std::string* fileattrstring,
                          const std::vector<std::string>& /*tempurls*/,
                          const std::vector<std::string>& /*ips*/,
                          const std::string& /*fileHandle*/)
                {
                    if (!fingerprint) // failed processing the command
                    {
                        if (e == API_ETOOMANY && e.hasExtraInfo())
                        {
                            cout << "Link check failed: " << DemoApp::getExtraInfoErrorString(e)
                                 << endl;
                        }
                        else
                        {
                            cout << "Link check failed: " << errorstring(e) << endl;
                        }
                        return true;
                    }

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

                        TransferDbCommitter committer(client->tctable);
                        auto file = std::make_unique<AppFileGet>(nullptr, NodeHandle().set6byte(ph), (byte*)key, size, 0, filename, fingerprint);
                        startxfer(committer, std::move(file), *filename, client->nextreqtag());
                    }

                    return true;
                }));

            return;
        }

        n = nodebypath(s.words[1].s.c_str());

        if (n)
        {
            if (s.words.size() > 2)
            {
                // read file slice
                m_off_t offset = atol(s.words[2].s.c_str());
                m_off_t count = (s.words.size() > 3) ? atol(s.words[3].s.c_str()) : 0;

                if (offset + count > n->size)
                {
                    if (offset < n->size)
                    {
                        count = n->size - offset;
                        cout << "Count adjusted to " << count << " bytes (filesize is " << n->size << " bytes)" << endl;
                    }
                    else
                    {
                        cout << "Nothing to read: offset + length > filesize (" << offset << " + " << count << " > " << n->size << " bytes)" << endl;
                        return;
                    }
                }

                if (s.words.size() == 5)
                {
                    pread_file = new ofstream(s.words[4].s.c_str(), std::ios_base::binary);
                    pread_file_end = offset + count;
                }

                client->pread(n.get(), offset, count, NULL);
            }
            else
            {
                TransferDbCommitter committer(client->tctable);

                // queue specified file...
                if (n->type == FILENODE)
                {
                    auto f = std::make_unique<AppFileGet>(n.get());

                    string::size_type index = s.words[1].s.find(":");
                    // node from public folder link
                    if (index != string::npos && s.words[1].s.substr(0, index).find("@") == string::npos)
                    {
                        handle h = clientFolder->mNodeManager.getRootNodeFiles().as8byte();
                        char *pubauth = new char[12];
                        Base64::btoa((byte*)&h, MegaClient::NODEHANDLE, pubauth);
                        f->pubauth = pubauth;
                        f->hprivate = true;
                        f->hforeign = true;
                        memcpy(f->filekey, n->nodekey().data(), FILENODEKEYLENGTH);
                    }

                    startxfer(committer, std::move(f), *n, client->nextreqtag());
                }
                else
                {
                    // ...or all files in the specified folder (non-recursive)
                    for (auto& node : client->getChildren(n.get()))
                    {
                        if (node->type == FILENODE)
                        {
                            auto f = std::make_unique<AppFileGet>(node.get());
                            startxfer(committer, std::move(f), *node.get(), client->nextreqtag());
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

/* more_node here is intentionally defined with filescope, it allows us to
 * resume an interrupted pagination.
 * Node contents are fetched one page at a time, defaulting to 1KB of data.
 * Improvement: Get console layout and use width*height for precise pagination.
 */
static std::shared_ptr<Node> more_node = nullptr; // Remote node that we are paging through
static m_off_t  more_offset = 0; // Current offset in the remote file
static const m_off_t MORE_BYTES = 1024;

void exec_more(autocomplete::ACState& s)
{
    if(s.words.size() > 1) // set up new node for pagination
    {
        more_offset = 0;
        more_node = nodebypath(s.words[1].s.c_str());
    }
    if(more_node && (more_node->type == FILENODE))
    {
        m_off_t count = (more_offset + MORE_BYTES <= more_node->size)
                ? MORE_BYTES : (more_node->size - more_offset);

        client->pread(more_node.get(), more_offset, count, NULL);
    }
}

void uploadLocalFolderContent(const LocalPath& localname, Node* cloudFolder, VersioningOption vo, bool allowDuplicateVersions);

void uploadLocalPath(nodetype_t type, std::string name, const LocalPath& localname, Node* parent, const std::string& targetuser,
    TransferDbCommitter& committer, int& total, bool recursive, VersioningOption vo,
    std::function<std::function<void()>(LocalPath)> onCompletedGenerator, bool noRetries, bool allowDuplicateVersions)
{

    std::shared_ptr<Node> previousNode = client->childnodebyname(parent, name.c_str(), false);

    if (type == FILENODE)
    {
        auto fa = client->fsaccess->newfileaccess();
        if (fa->fopen(localname, true, false, FSLogging::logOnError))
        {
            FileFingerprint fp;
            fp.genfingerprint(fa.get());

            if (previousNode)
            {
                if (previousNode->type == FILENODE)
                {
                    if (!allowDuplicateVersions && fp.isvalid && previousNode->isvalid && fp == *((FileFingerprint *)previousNode.get()))
                    {
                        cout << "Identical file already exist. Skipping transfer of " << name << endl;
                        return;
                    }
                }
                else
                {
                    cout << "Can't upload file over the top of a folder with the same name: " << name << endl;
                    return;
                }
            }
            fa.reset();

            AppFilePut* f = new AppFilePut(localname, parent ? parent->nodeHandle() : NodeHandle(), targetuser.c_str());
            f->noRetries = noRetries;

            if (onCompletedGenerator) f->onCompleted = onCompletedGenerator(localname);
            *static_cast<FileFingerprint*>(f) = fp;
            f->appxfer_it = appxferq[PUT].insert(appxferq[PUT].end(), f);
            client->startxfer(PUT, f, committer, false, false, false, vo, nullptr, client->nextreqtag());
            total++;
        }
        else
        {
            cout << "Can't open file: " << name << endl;
        }
    }
    else if (type == FOLDERNODE && recursive)
    {

        if (previousNode)
        {
            if (previousNode->type == FILENODE)
            {
                cout << "Can't upload a folder over the top of a file with the same name: " << name << endl;
                return;
            }
            else
            {
                // upload into existing folder with the same name
                uploadLocalFolderContent(localname, previousNode.get(), vo, true);
            }
        }
        else
        {
            vector<NewNode> nn(1);
            client->putnodes_prepareOneFolder(&nn[0], name, false);

            gOnPutNodeTag[gNextClientTag] = [localname, vo](Node* parent) {
                auto tmp = localname;
                uploadLocalFolderContent(tmp, parent, vo, true);
            };

            client->putnodes(parent->nodeHandle(), NoVersioning, std::move(nn), nullptr, gNextClientTag++, false);
        }
    }
}


string localpathToUtf8Leaf(const LocalPath& itemlocalname)
{
    return itemlocalname.leafName().toPath(true);  // true since it's used for upload
}

void uploadLocalFolderContent(const LocalPath& localname, Node* cloudFolder, VersioningOption vo, bool allowDuplicateVersions)
{
#ifndef DONT_USE_SCAN_SERVICE

    auto fa = client->fsaccess->newfileaccess();
    fa->fopen(localname, FSLogging::logOnError);
    if (fa->type != FOLDERNODE)
    {
        cout << "Path is not a folder: " << localname.toPath(false);
        return;
    }

    ScanService s;
    ScanService::RequestPtr r = s.queueScan(localname, fa->fsid, false, {}, client->waiter);

    while (!r->completed())
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if (r->completionResult() != SCAN_SUCCESS)
    {
        cout << "Scan failed: " << r->completionResult() << " for path: " << localname.toPath(false);
        return;
    }

    std::vector<FSNode> results = r->resultNodes();

    TransferDbCommitter committer(client->tctable);
    int total = 0;

    for (auto& rr : results)
    {
        auto newpath = localname;
        newpath.appendWithSeparator(rr.localname, true);
        uploadLocalPath(rr.type, rr.localname.toPath(false), newpath, cloudFolder, "", committer, total, true, vo, nullptr, false, allowDuplicateVersions);
    }

    if (gVerboseMode)
    {
        cout << "Queued " << total << " more uploads from folder " << localname.toPath(false) << endl;
    }

#else

    auto da = client->fsaccess->newdiraccess();

    LocalPath lp(localname);
    if (da->dopen(&lp, NULL, false))
    {
        TransferDbCommitter committer(client->tctable);

        int total = 0;
        nodetype_t type;
        LocalPath itemlocalleafname;
        while (da->dnext(lp, itemlocalleafname, true, &type))
        {
            string leafNameUtf8 = localpathToUtf8Leaf(itemlocalleafname);

            if (gVerboseMode)
            {
                cout << "Queueing " << leafNameUtf8 << "..." << endl;
            }
            auto newpath = lp;
            newpath.appendWithSeparator(itemlocalleafname, true);
            uploadLocalPath(type, leafNameUtf8, newpath, cloudFolder, "", committer, total, true, vo, nullptr, true);
        }
        if (gVerboseMode)
        {
            cout << "Queued " << total << " more uploads from folder " << localpathToUtf8Leaf(localname) << endl;
        }
    }
#endif
}

void exec_put(autocomplete::ACState& s)
{
    NodeHandle target = cwd;
    string targetuser;
    string newname;
    int total = 0;
    std::shared_ptr<Node> n;

    VersioningOption vo = UseLocalVersioningFlag;
    if (s.extractflag("-noversion")) vo = NoVersioning;
    if (s.extractflag("-version")) vo = ClaimOldVersion;
    if (s.extractflag("-versionreplace")) vo = ReplaceOldVersion;
    bool allowDuplicateVersions = s.extractflag("-allowduplicateversions");

    bool recursive = s.extractflag("-r");

    if (s.words.size() > 2)
    {
        if ((n = nodebypath(s.words[2].s.c_str(), &targetuser, &newname)))
        {
            target = n->nodeHandle();
        }
    }
    else    // target is current path
    {
        n = client->nodeByHandle(target);
    }

    if (client->loggedin() == NOTLOGGEDIN && !targetuser.size() && !client->loggedIntoWritableFolder())
    {
        cout << "Not logged in." << endl;

        return;
    }

    if (recursive && !targetuser.empty())
    {
        cout << "Sorry, can't send recursively to a user" << endl;
    }

    auto localname = localPathArg(s.words[1].s);

    auto da = client->fsaccess->newdiraccess();

    // search with glob, eg *.txt
    if (da->dopen(&localname, NULL, true))
    {
        TransferDbCommitter committer(client->tctable);

        nodetype_t type;
        LocalPath itemlocalname;
        while (da->dnext(localname, itemlocalname, true, &type))
        {
            string leafNameUtf8 = localpathToUtf8Leaf(itemlocalname);

            if (gVerboseMode)
            {
                cout << "Queueing " << leafNameUtf8 << "..." << endl;
            }
            uploadLocalPath(type, leafNameUtf8, itemlocalname, n.get(), targetuser, committer, total, recursive, vo, nullptr, false, allowDuplicateVersions);
        }
    }

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
    if (s.words.size() != 2)
    {
        cout << "lcd <dir>" << endl;
        return;
    }

    LocalPath localpath = localPathArg(s.words[1].s);

    if (!client->fsaccess->chdirlocal(localpath))
    {
        cout << s.words[1].s << ": Failed" << endl;
    }
}


void exec_llockfile(autocomplete::ACState& s)
{
    bool readlock = s.extractflag("-read");
    bool writelock = s.extractflag("-write");
    bool unlock = s.extractflag("-unlock");

    if (!readlock && !writelock && !unlock)
    {
        readlock = true;
        writelock = true;
    }

    LocalPath localpath = localPathArg(s.words[1].s);

#ifdef WIN32
    static map<LocalPath, HANDLE> llockedFiles;

    if (unlock)
    {
        if (llockedFiles.find(localpath) == llockedFiles.end()) return;
        CloseHandle(llockedFiles[localpath]);
    }
    else
    {
        string pe = localpath.platformEncoded();
        HANDLE hFile = CreateFileW(wstring((wchar_t*)pe.data(), pe.size()/2).c_str(),
            readlock ? GENERIC_READ : (writelock ? GENERIC_WRITE : 0),
            0, // no sharing
            NULL, OPEN_EXISTING, 0, NULL);

        if (hFile == INVALID_HANDLE_VALUE)
        {
            auto err = GetLastError();
            cout << "Error locking file: " << err;
        }
        else
        {
            llockedFiles[localpath] = hFile;
        }
    }

#else
    cout << " sorry, not implemented yet" << endl;
#endif
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
    bool showActive = s.extractflag("-active");
    bool showAll = s.extractflag("-all");
    bool showCount = s.extractflag("-count");

    if (!showActive && !showAll && !showCount)
    {
        showCount = true;
    }

    xferq(PUT, s.words.size() > 1 ? atoi(s.words[1].s.c_str()) : -1, showActive, showAll, showCount);
}

void exec_getq(autocomplete::ACState& s)
{
    bool showActive = s.extractflag("-active");
    bool showAll = s.extractflag("-all");
    bool showCount = s.extractflag("-count");

    if (!showActive && !showAll && !showCount)
    {
        showCount = true;
    }

    xferq(GET, s.words.size() > 1 ? atoi(s.words[1].s.c_str()) : -1, showActive, showAll, showCount);
}

void exec_open(autocomplete::ACState& s)
{
    if (strstr(s.words[1].s.c_str(), "#F!") || strstr(s.words[1].s.c_str(), "folder/"))  // folder link indicator
    {
        if (!clientFolder)
        {
            using namespace mega;

            auto provider = IGfxProvider::createInternalGfxProvider();
            GfxProc* gfx = provider ? new GfxProc(std::move(provider)) : nullptr;
            if (gfx) gfx->startProcessingThread();

            // create a new MegaClient with a different MegaApp to process callbacks
            // from the client logged into a folder. Reuse the waiter and httpio
            clientFolder =
                new MegaClient(new DemoAppFolder,
                               client->waiter,
                               client->httpio,
#ifdef DBACCESS_CLASS
                               new DBACCESS_CLASS(*startDir),
#else
                               NULL,
#endif
                               gfx,
                               "Gk8DyQBS",
                               "megacli_folder/" TOSTRING(MEGA_MAJOR_VERSION) "." TOSTRING(
                                   MEGA_MINOR_VERSION) "." TOSTRING(MEGA_MICRO_VERSION),
                               2,
                               client->getClientType());
        }
        else
        {
            clientFolder->logout(false);
        }

        const char* authToken = nullptr;

        if (s.words.size() > 2)
            authToken = s.words[2].s.c_str();

        return clientFolder->app->login_result(clientFolder->folderaccess(s.words[1].s.c_str(), authToken));
    }
    else
    {
        cout << "Invalid folder link." << endl;
    }
}
#ifdef ENABLE_SYNC

void exec_syncrescan(autocomplete::ACState& s)
{
    handle backupId = 0;
    Base64::atob(s.words[2].s.c_str(), (byte*)&backupId, int(sizeof(backupId)));

    client->syncs.setSyncsNeedFullSync(true, true, backupId);
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
    string email;
    if (s.words.size() == 2)
    {
        email = s.words[1].s;
    }
    else
    {
        email = login.email;
    }

    client->multifactorauthcheck(email.c_str());
}

void exec_mfae(autocomplete::ACState& s)
{
    client->multifactorauthsetup();
}

void exec_login(autocomplete::ACState& s)
{
    //bool fresh = s.extractflag("-fresh");
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
                    cout << "Resuming session..." << endl;
                    return client->login(Base64::atob(session));
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
                    const char *authKey = s.words.size() == 3 ? s.words[2].s.c_str() : nullptr;
                    return client->app->login_result(client->folderaccess(s.words[1].s.c_str(), authKey));
                }
                else
                {
                    return client->login(Base64::atob(s.words[1].s));
                }
            }
        }
        else
        {
            cout << "      login email [password]" << endl
                << "      login exportedfolderurl#key [authKey]" << endl
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
    bool ephemeralPlusPlus = s.extractflag("-e++");
    if (s.words.size() == 1)
    {
        cout << "Creating ephemeral session..." << endl;

        pdf_to_import = true;
        client->createephemeral();
    }
    else if (s.words.size() == 2)   // resume session
    {
        if (ephemeralPlusPlus)
        {
            client->resumeephemeralPlusPlus(Base64::atob(s.words[1].s));
        }
        else
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
    else if (ephemeralPlusPlus && s.words.size() == 3)  // begin -e++ firstname lastname
    {
        cout << "Creating ephemeral session plus plus..." << endl;

        pdf_to_import = true;
        ephemeralFirstname = s.words[1].s;
        ephemeralLastName = s.words[2].s;
        client->createephemeralPlusPlus();
    }
}

void exec_mount(autocomplete::ACState& )
{
    listtrees();
}

void exec_share(autocomplete::ACState& s)
{
    bool writable = false;

    switch (s.words.size())
    {
    case 1:		// list all shares (incoming, outgoing and pending outgoing)
    {
        listallshares();
    }
    break;

    case 2:	    // list all outgoing shares on this path
    case 3:	    // remove outgoing share to specified e-mail address
    case 4:	    // add outgoing share to specified e-mail address
    case 5:     // user specified a personal representation to appear as for the invitation
        if (std::shared_ptr<Node> n = nodebypath(s.words[1].s.c_str()))
        {
            if (s.words.size() == 2)
            {
                listnodeshares(n.get(), false);
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

                handle nodehandle = n->nodehandle;
                std::function<void()> completeShare = [nodehandle, s, a, writable, personal_representation]()
                {
                    std::shared_ptr<Node> n = client->nodebyhandle(nodehandle);
                    if (!n)
                    {
                        cout << "Node not found." << endl;
                        return;
                    }

                    client->setshare(n, s.words[2].s.c_str(), a, writable, personal_representation, gNextClientTag++, [](Error e, bool)
                    {
                        if (e)
                        {
                            cout << "Share creation/modification request failed (" << errorstring(e) << ")" << endl;
                        }
                        else
                        {
                            cout << "Share creation/modification succeeded." << endl;
                        }
                    });
                };

                if (a != ACCESS_UNKNOWN)
                {
                    client->openShareDialog(n.get(), [completeShare](Error e)
                    {
                        if (e)
                        {
                            cout << "Error creating share key (" << errorstring(e) << ")" << endl;
                            return;
                        }

                        completeShare();
                    });
                    return;
                }
                completeShare();
            }
        }
        else
        {
            cout << s.words[1].s << ": No such directory" << endl;
        }
        break;
    }
}

void exec_getemail(autocomplete::ACState& s)
{
    if (!client->loggedin())
    {
        cout << "Must be logged in to fetch user emails" << endl;
        return;
    }

    client->getUserEmail(s.words[1].s.c_str());
}
void DemoApp::getuseremail_result(string *email, error e)
{
    if (e)
    {
        cout << "Failed to retrieve email: " << e << endl;
    }
    else
    {
        cout << "Email: " << email << endl;
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
                cout << "\t" << it->second.email << " (" << toHandle(it->second.userhandle) << ")";

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

                if (it->second.userhandle != client->me && client->areCredentialsVerified(it->second.userhandle))
                {
                    cout << ", credentials verified";
                }

                if (it->second.sharing.size())
                {
                    cout << ", sharing " << it->second.sharing.size() << " folder(s)";
                }

                if (it->second.pubk.isvalid())
                {
                    cout << ", public key cached";
                }

                if (it->second.mBizMode == BIZ_MODE_MASTER)
                {
                    cout << ", business master user";
                }
                else if (it->second.mBizMode == BIZ_MODE_SUBUSER)
                {
                    cout << ", business sub-user";
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
    bool allowDuplicate = s.extractflag("-allowduplicate");
    bool exactLeafName = s.extractflag("-exactleafname");
    bool writevault = s.extractflag("-writevault");

    if (s.words.size() > 1)
    {
        string newname;

        std::shared_ptr<Node> n;
        if (exactLeafName)
        {
            n = client->nodeByHandle(cwd);
            newname = s.words[1].s;
        }
        else
        {
            n = nodebypath(s.words[1].s.c_str(), NULL, &newname);
        }

        if (n)
        {
            if (!client->checkaccess(n.get(), RDWR))
            {
                cout << "Write access denied" << endl;

                return;
            }

            if (newname.size())
            {
                vector<NewNode> nn(1);
                client->putnodes_prepareOneFolder(&nn[0], newname, writevault);
                client->putnodes(n->nodeHandle(), NoVersioning, std::move(nn), nullptr, gNextClientTag++, writevault);
            }
            else if (allowDuplicate && n->parent && n->parent->nodehandle != UNDEF)
            {
                // the leaf name already exists and was returned in n
                auto leafname = s.words[1].s;
                auto pos = leafname.find_last_of("/");
                if (pos != string::npos) leafname.erase(0, pos + 1);
                vector<NewNode> nn(1);
                client->putnodes_prepareOneFolder(&nn[0], leafname, writevault);
                client->putnodes(n->parent->nodeHandle(), NoVersioning, std::move(nn), nullptr, gNextClientTag++, writevault);
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
    std::shared_ptr<Node> n;
    int cancel = s.words.size() > 2 && s.words.back().s == "cancel";

    if (s.words.size() < 3)
    {
        n = client->nodeByHandle(cwd);
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
                client->getfa(n->nodehandle, &n->fileattrstring, n->nodekey(), type, cancel);
                c++;
            }
        }
        else
        {
            for (auto& node : client->getChildren(n.get()))
            {
                if (node->type == FILENODE && node->hasfileattribute(type))
                {
                    client->getfa(node->nodehandle, &node->fileattrstring, node->nodekey(), type, cancel);
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

    if (s.words[1].s == "pubk")
    {
        client->getpubkey(u->uid.c_str());
        return;
    }

    client->getua(u, User::string2attr(s.words[1].s.c_str()));
}

void exec_putua(autocomplete::ACState& s)
{

    if (!client->loggedin())
    {
        cout << "Must be logged in to set user attributes." << endl;
        return;
    }

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
            byte* value = new byte[static_cast<size_t>(len)];
            int valuelen = Base64::atob(s.words[3].s.data(), value, len);
            client->putua(attrtype, value, static_cast<unsigned>(valuelen));
            delete [] value;
            return;
        }
        else if (s.words[2].s == "load")
        {
            string data;
            auto localpath = localPathArg(s.words[3].s);

            if (loadfile(localpath, &data))
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
    else if (s.words.size() == 5)
    {
        if (s.words[2].s == "map")  // putua <attrtype> map <attrKey> <attrValue>
        {
            // received <attrKey> will be B64 encoded
            // received <attrValue> will have the real text value
            if (attrtype == ATTR_DEVICE_NAMES       // TLV: { B64enc DeviceId hash, device name } or { ext:B64enc DriveId, drive name }
                    || attrtype == ATTR_ALIAS)      // TLV: { B64enc User handle, alias }
            {
                putua_map(s.words[3].s, Base64::btoa(s.words[4].s), attrtype);
            }
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

    TransferDbCommitter committer(client->tctable);

    if (getarg)
    {
        client->pausexfers(GET, client->xferpaused[GET] ^= true, hardarg, committer);
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
        client->pausexfers(PUT, client->xferpaused[PUT] ^= true, hardarg, committer);
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
    if (s.extractflag("-off"))
    {
        SimpleLogger::setLogLevel(logWarning);
        gLogger.logToConsole = false;
        gLogger.mLogFile.close();
    }
    if (s.extractflag("-on"))
    {
        SimpleLogger::setLogLevel(logDebug);
    }
    if (s.extractflag("-verbose"))
    {
        SimpleLogger::setLogLevel(logMax);
    }
    if (s.extractflag("-console"))
    {
        gLogger.logToConsole = true;

    }
    if (s.extractflag("-noconsole"))
    {
        gLogger.logToConsole = false;
    }
    if (s.extractflag("-nofile"))
    {
        gLogger.mLogFile.close();
    }
    string filename;
    if (s.extractflagparam("-file", filename))
    {
        gLogger.mLogFile.close();
        if (!filename.empty())
        {
            gLogger.mLogFile.open(filename.c_str());
            if (gLogger.mLogFile.is_open())
            {
                gLogger.mLogFileName = filename;

            }
            else
            {
                cout << "Log file open failed: '" << filename << "'" << endl;
            }
        }
    }

    cout << "Debug level set to " << SimpleLogger::getLogLevel() << endl;
    cout << "Log to console: " << (gLogger.logToConsole ? "on" : "off") << endl;
    cout << "Log to file: " << (gLogger.mLogFile.is_open() ? gLogger.mLogFileName : "<off>") << endl;

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

            size_t pos = link.find(MegaClient::verifyLinkPrefix());
            if (pos == link.npos)
            {
                cout << "Invalid email change link." << endl;
                return;
            }

            changecode.assign(link.substr(pos + strlen(MegaClient::verifyLinkPrefix())));
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

        client->createChat(group != 0, false, userpriv);
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
        cout << "Current APIURL = " << client->httpio->APIURL << endl;
        cout << "Current disablepkp = " << (client->httpio->disablepkp ? "true" : "false") << endl;
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
        client->httpio->APIURL = s.words[1].s;
        if (s.words.size() == 3)
        {
            client->httpio->disablepkp = s.words[2].s == "true";
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

        if ((tptr = strstr(ptr, "confirm")))
        {
            ptr = tptr + 7;

            std::string code = Base64::atob(std::string(ptr));
            if (code.find("ConfirmCodeV2") != string::npos)
            {
                size_t posEmail = 13 + 15;
                size_t endEmail = code.find("\t", posEmail);
                if (endEmail != string::npos)
                {
                    signupemail = code.substr(posEmail, endEmail - posEmail);
                    signupname = code.substr(endEmail + 1, code.size() - endEmail - 9);

                    if (client->loggedin() == FULLACCOUNT)
                    {
                        cout << "Already logged in." << endl;
                    }
                    else    // not-logged-in / ephemeral account / partially confirmed
                    {
                        client->confirmsignuplink2((const byte*)code.data(), unsigned(code.size()));
                    }
                }
            }
            else
            {
                cout << "Received argument was not a confirmation link." << endl;
            }
        }
        else
        {
            cout << "New accounts must follow registration flow v2. Old flow is not supported anymore." << endl;
        }
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
        case EPHEMERALACCOUNTPLUSPLUS:
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
            if (client->signkey)
            {
                string pubKey((const char *)client->signkey->pubKey, EdDSA::PUBLIC_KEY_LENGTH);
                cout << "Credentials: " << AuthRing::fingerprint(pubKey, true) << endl;
            }
        }

        bool storage = s.extractflag("-storage");
        bool transfer = s.extractflag("-transfer");
        bool pro = s.extractflag("-pro");
        bool transactions = s.extractflag("-transactions");
        bool purchases = s.extractflag("-purchases");
        bool sessions = s.extractflag("-sessions");

        bool all = !storage && !transfer && !pro && !transactions && !purchases && !sessions;

        cout << "Retrieving account status..." << endl;

        client->getaccountdetails(account, all || storage, all || transfer, all || pro, all || transactions, all || purchases, all || sessions);
    }
}

void exec_verifycredentials(autocomplete::ACState& s)
{
    User* u = nullptr;
    if (s.words.size() == 2 && (s.words[1].s == "show" || s.words[1].s == "status"))
    {
        u = client->finduser(client->me);
    }
    else if (s.words.size() == 3)
    {
        u = client->finduser(s.words[2].s.c_str());
    }
    else
    {
        cout << "      credentials show|status|verify|reset [email]" << endl;
        return;
    }

    if (!u)
    {
        cout << "Invalid user" << endl;
        return;
    }

    if (s.words[1].s == "show")
    {
        const UserAttribute* attribute = u->getAttribute(ATTR_ED25519_PUBK);
        if (attribute && attribute->isValid())
        {
            cout << "Credentials: " << AuthRing::fingerprint(attribute->value(), true) << endl;
        }
        else
        {
            cout << "Fetching singing key... " << endl;
            client->getua(u->uid.c_str(), ATTR_ED25519_PUBK);
        }
    }
    else if (s.words[1].s == "status")
    {
        handle uh = s.words.size() == 3 ? u->userhandle : UNDEF;
        printAuthringInformation(uh);
    }
    else if (s.words[1].s == "verify")
    {
        error e;
        if ((e = client->verifyCredentials(u->userhandle, nullptr)))
        {
            cout << "Verification failed. Error: " << errorstring(e) << endl;
            return;
        }
    }
    else if (s.words[1].s == "reset")
    {
        error e;
        if ((e = client->resetCredentials(u->userhandle, nullptr)))
        {
            cout << "Reset verification failed. Error: " << errorstring(e) << endl;
            return;
        }
    }
}

void exec_export(autocomplete::ACState& s)
{
    void exportnode_result(Error e, handle h, handle ph);

    std::shared_ptr<Node> n;
    int deltmp = 0;
    int etstmp = 0;

    bool writable = s.extractflag("-writable");
    bool megaHosted = s.extractflag("-mega-hosted");


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
        if ((e = client->exportnode(n,
                                    deltmp,
                                    etstmp,
                                    writable,
                                    megaHosted,
                                    gNextClientTag++,
                                    [](Error e, handle h, handle ph, string&&)
                                    {
                                        exportnode_result(e, h, ph);
                                    })))
        {
            cout << s.words[1].s << ": Export rejected (" << errorstring(e) << ")" << endl;
        }
    }
    else
    {
        cout << s.words[1].s << ": Not found" << endl;
    }
}

void exec_encryptLink(autocomplete::ACState& s)
{
    string link = s.words[1].s;
    string password = s.words[2].s;
    string encryptedLink;

    error e = client->encryptlink(link.c_str(), password.c_str(), &encryptedLink);
    if (e)
    {
        cout << "Failed to encrypt link: " << errorstring(e) << endl;
    }
    else
    {
        cout << "Password encrypted link: " << encryptedLink << endl;
    }
}

void exec_decryptLink(autocomplete::ACState &s)
{
    string link = s.words[1].s;
    string password = s.words[2].s;
    string decryptedLink;

    error e = client->decryptlink(link.c_str(), password.c_str(), &decryptedLink);
    if (e)
    {
        cout << "Failed to decrypt link: " << errorstring(e) << endl;
    }
    else
    {
        cout << "Decrypted link: " << decryptedLink << endl;
    }

}

void exec_import(autocomplete::ACState& s)
{
    handle ph = UNDEF;
    byte key[FILENODEKEYLENGTH];
    error e = client->parsepubliclink(s.words[1].s.c_str(), ph, key, TypeOfLink::FILE);
    if (e == API_OK)
    {
        cout << "Opening link..." << endl;
        client->openfilelink(ph, key);
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
    byte folderkey[FOLDERNODEKEYLENGTH];
    if (client->parsepubliclink(publiclink.c_str(), ph, folderkey, TypeOfLink::FOLDER) == API_OK)
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

    cwd = NodeHandle();
    client->cachedscsn = UNDEF;
    client->fetchnodes(nocache, false, true);
}

void exec_logout(autocomplete::ACState& s)
{
    cout << "Logging off..." << endl;

    bool keepSyncConfigs = s.extractflag("-keepsyncconfigs");

    cwd = NodeHandle();
    client->logout(keepSyncConfigs);

    if (clientFolder)
    {
        clientFolder->logout(keepSyncConfigs);
        delete clientFolder;
        clientFolder = NULL;
    }

    ephemeralFirstname.clear();
    ephemeralLastName.clear();
}

#ifdef ENABLE_CHAT
void exec_chatga(autocomplete::ACState& s)
{
    handle chatid;
    Base64::atob(s.words[1].s.c_str(), (byte*) &chatid, MegaClient::CHATHANDLE);

    handle nodehandle = 0; // make sure top two bytes are 0
    Base64::atob(s.words[2].s.c_str(), (byte*) &nodehandle, MegaClient::NODEHANDLE);

    const char *uid = s.words[3].s.c_str();

    client->grantAccessInChat(chatid, nodehandle, uid);
}

void exec_chatra(autocomplete::ACState& s)
{
    handle chatid;
    Base64::atob(s.words[1].s.c_str(), (byte*)&chatid, MegaClient::CHATHANDLE);

    handle nodehandle = 0; // make sure top two bytes are 0
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
    bool meeting = s.extractflag("-meeting");
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
            userkeymap->insert(StringPair(uhB64, unifiedkey));
            numUsers++;
        }
    }
    char ownHandleB64[12];
    Base64::btoa((byte *)&client->me, MegaClient::USERHANDLE, ownHandleB64);
    ownHandleB64[11] = '\0';
    userkeymap->insert(StringPair(ownHandleB64, mownkey));
    client->createChat(true, true, userpriv, userkeymap, title, meeting);
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

        size_t pos = link.find(MegaClient::cancelLinkPrefix());
        if (pos == link.npos)
        {
            cout << "Invalid cancellation link." << endl;
            return;
        }

        client->confirmcancellink(link.substr(pos + strlen(MegaClient::cancelLinkPrefix())).c_str());
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
            client->useralerts.add(new UserAlert::Payment(true,
                                                          1,
                                                          time(NULL) + 86000 * 1,
                                                          client->useralerts.nextId(),
                                                          name_id::psts));
        }
        else if (s.words[1].s == "test_payment_v2")
        {
            client->useralerts.add(new UserAlert::Payment(true,
                                                          1,
                                                          time(NULL) + 86000 * 1,
                                                          client->useralerts.nextId(),
                                                          name_id::psts_v2));
        }
        else if (atoi(s.words[1].s.c_str()) > 0)
        {
            showN = static_cast<size_t>(atoi(s.words[1].s.c_str()));
        }
    }
    if (showold || shownew || showN > 0)
    {
        UserAlerts::Alerts::const_iterator i = client->useralerts.alerts.begin();
        if (showN)
        {
            size_t n = 0;
            for (UserAlerts::Alerts::const_reverse_iterator j = client->useralerts.alerts.rbegin(); j != client->useralerts.alerts.rend(); ++j, ++n)
            {
                if (!(*j)->removed())
                {
                    showN += ((*j)->relevant() || n >= showN) ? 0 : 1;
                }
            }
        }

        size_t n = client->useralerts.alerts.size();
        for (; i != client->useralerts.alerts.end(); ++i)
        {
            if ((*i)->relevant() && !(*i)->removed())
            {
                if (--n < showN || (shownew && !(*i)->seen()) || (showold && (*i)->seen()))
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

void exec_recover(autocomplete::ACState& s)
{
    if (client->loggedin() != NOTLOGGEDIN)
    {
        cout << "You're logged in. Please, logout first." << endl;
    }
    else if (s.words.size() == 2)
    {
        string link = s.words[1].s;

        size_t pos = link.find(MegaClient::recoverLinkPrefix());
        if (pos == link.npos)
        {
            cout << "Invalid recovery link." << endl;
        }

        recoverycode.assign(link.substr(pos + strlen(MegaClient::recoverLinkPrefix())));
        client->queryrecoverylink(recoverycode.c_str());
    }
}

void exec_session(autocomplete::ACState& s)
{
    string session;

    int size = client->dumpsession(session);

    if (size > 0)
    {
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
                file << Base64::btoa(session);
                cout << "Your (secret) session is saved in file '" << filename << "'" << endl;
            }
        }
        else
        {
            cout << "Your (secret) session is: " << Base64::btoa(session) << endl;
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

#ifdef HAVE_PDFIUM
    cout << "* PDFium" << endl;
#endif

#ifdef HAVE_FFMPEG
    cout << "* FFmpeg" << endl;
#endif

#ifdef ENABLE_SYNC
    cout << "* sync subsystem" << endl;
#endif

#ifdef USE_MEDIAINFO
    cout << "* MediaInfo" << endl;
#endif

    cwd = NodeHandle();
}

void exec_showpcr(autocomplete::ACState& s)
{
    string outgoing;
    string incoming;
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
        client->mediaFileInfo.requestCodecMappingsOneTime(client, LocalPath());
        cout << "Mediainfo lookups requested" << endl;
    }

    if (s.words.size() == 3 && s.words[1].s == "calc")
    {
        MediaProperties mp;
        auto localFilename = localPathArg(s.words[2].s);

        string ext;
        if (client->fsaccess->getextension(localFilename, ext) && MediaProperties::isMediaFilenameExt(ext))
        {
            mp.extractMediaPropertyFileAttributes(localFilename, client->fsaccess.get());
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
        if (std::shared_ptr<Node> n = nodebypath(s.words[2].s.c_str()))
        {
            switch (n->type)
            {
            case FILENODE:
                cout << showMediaInfo(n.get(), client->mediaFileInfo, false) << endl;
                break;

            case FOLDERNODE:
            case ROOTNODE:
            case VAULTNODE:
            case RUBBISHNODE:
            {
                for (auto& m : client->getChildren(n.get()))
                {
                    if (m->type == FILENODE && m->hasfileattribute(fa_media))
                    {
                        cout << m->displayname() << "   " << showMediaInfo(m.get(), client->mediaFileInfo, true) << endl;
                    }
                }
                break;
            }
            case TYPE_DONOTSYNC:
            case TYPE_NESTED_MOUNT:
            case TYPE_SPECIAL:
            case TYPE_SYMLINK:
            case TYPE_UNKNOWN:
                cout << "node type is inappropriate for mediainfo: " << n->type << endl;
                break;
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

    cwd = NodeHandle();
    client->locallogout(false, true);

    ephemeralFirstname.clear();
    ephemeralLastName.clear();
}

void exec_recentnodes(autocomplete::ACState& s)
{
    if (s.words.size() == 3)
    {
        int maxElements{};
        if (auto [ptr, ec] = std::from_chars(s.words[2].s.data(),
                                             s.words[2].s.data() + s.words[2].s.size(),
                                             maxElements);
            ec != std::errc{})
        {
            std::cout << "Invalid max elements parameter" << endl;
            return;
        }

        int time{};
        if (auto [ptr, ec] = std::from_chars(s.words[1].s.data(),
                                             s.words[1].s.data() + s.words[1].s.size(),
                                             time);
            ec != std::errc{})
        {
            std::cout << "Invalid duration parameter" << endl;
            return;
        }

        NodeSearchFilter filter;
        filter.byAncestors({client->mNodeManager.getRootNodeFiles().as8byte(),
                            client->mNodeManager.getRootNodeVault().as8byte(),
                            UNDEF});

        filter.byCreationTimeLowerLimitInSecs(m_time() - 60 * 60 * time);
        filter.bySensitivity(NodeSearchFilter::BoolFilter::onlyTrue);
        filter.byNodeType(FILENODE);
        filter.setIncludedShares(IN_SHARES);
        sharedNode_vector nv =
            client->mNodeManager.searchNodes(filter,
                                             OrderByClause::CTIME_DESC,
                                             CancelToken(),
                                             NodeSearchPage{0, static_cast<size_t>(maxElements)});

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
    const bool excludeSens = s.extractflag("-nosensitive");
    recentactions_vector nvv =
        client->getRecentActions(static_cast<unsigned>(atoi(s.words[2].s.c_str())),
                                 m_time() - 60 * 60 * atoi(s.words[1].s.c_str()),
                                 excludeSens);

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

void exec_setmaxloglinesize(autocomplete::ACState& s)
{
    if (s.words.size() > 1)
    {
        SimpleLogger::setMaxPayloadLogSize(atoll(s.words[1].s.c_str()));
    }
}

void exec_drivemonitor(autocomplete::ACState& s)
{
#ifdef USE_DRIVE_NOTIFICATIONS

    bool turnon = s.extractflag("-on");
    bool turnoff = s.extractflag("-off");

    if (turnon)
    {
        // start receiving notifications
        if (!client->startDriveMonitor())
        {
            // return immediately, when this functionality was not implemented
            cout << "Failed starting drive notifications" << endl;
        }
    }
    else if (turnoff)
    {
        client->stopDriveMonitor();
    }

    cout << "Drive monitor " << (client->driveMonitorEnabled() ? "on" : "off") << endl;
#else
    std::cout << "Failed! This functionality was disabled at compile time." << std::endl;
#endif // USE_DRIVE_NOTIFICATIONS
}

void exec_driveid(autocomplete::ACState& s)
{
    auto drivePath = s.words[2].s.c_str();
    auto get = s.words[1].s == "get";
    auto force = s.words.size() == 4;

    if (!force)
    {
        auto id = UNDEF;
        auto result = readDriveId(*client->fsaccess, drivePath, id);

        switch (result)
        {
        case API_ENOENT:
            if (!get) break;

            cout << "No drive ID has been assigned to "
                 << drivePath
                 << endl;
            return;

        case API_EREAD:
            cout << "Unable to read drive ID from "
                 << drivePath
                 << endl;
            return;

        case API_OK:
            cout << "Drive "
                 << drivePath
                 << " has the ID "
                 << toHandle(id)
                 << endl;
            return;

        default:
            assert(false && "Unexpected result from readDriveID(...)");
            cerr << "Unexpected result from readDriveId(...): "
                 << errorstring(result)
                 << endl;
            return;
        }
    }

    auto id = generateDriveId(client->rng);
    auto result = writeDriveId(*client->fsaccess, drivePath, id);

    if (result != API_OK)
    {
        cout << "Unable to write drive ID to "
             << drivePath
             << endl;
        return;
    }

    cout << "Drive ID "
         << toHandle(id)
         << " has been written to "
         << drivePath
         << endl;
}

void exec_randomfile(autocomplete::ACState& s)
{
    // randomfile path [length]
    auto length = 2l;

    if (s.words.size() > 2)
        length = std::atol(s.words[2].s.c_str());

    if (length <= 0)
    {
        std::cerr << "Invalid length specified: "
                  << s.words[2].s
                  << std::endl;
        return;
    }

    constexpr auto flags =
      std::ios::binary | std::ios::out | std::ios::trunc;

    std::ofstream ostream(s.words[1].s, flags);

    if (!ostream)
    {
        std::cerr << "Unable to open file for writing: "
                  << s.words[1].s
                  << std::endl;
        return;
    }

    std::generate_n(std::ostream_iterator<char>(ostream),
                    length << 10,
                    []() { return (char)std::rand(); });

    if (!ostream.flush())
    {
        std::cerr << "Encountered an error while writing: "
                  << s.words[1].s
                  << std::endl;
        return;
    }

    std::cout << "Successfully wrote "
              << length
              << " kilobytes of random binary data to: "
              << s.words[1].s
              << std::endl;
}

#ifdef USE_DRIVE_NOTIFICATIONS
void DemoApp::drive_presence_changed(bool appeared, const LocalPath& driveRoot)
{
    std::cout << "Drive " << (appeared ? "connected" : "disconnected") << ": " << driveRoot.platformEncoded() << endl;
}
#endif // USE_DRIVE_NOTIFICATIONS

// callback for non-EAGAIN request-level errors
// in most cases, retrying is futile, so the application exits
// this can occur e.g. with syntactically malformed requests (due to a bug), an invalid application key
void DemoApp::request_error(error e)
{
    if ((e == API_ESID) || (e == API_ENOENT))   // Invalid session or Invalid folder handle
    {
        cout << "Invalid or expired session, logging out..." << endl;
        client->locallogout(true, true);
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

#ifndef NO_READLINE
    rl_callback_handler_remove();
#endif /* ! NO_READLINE */

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
        cout << "Login successful." << endl;
        // Delay fetchnodes to give time to the SDK to finish and unlock the internal
        // "nodeTreeMutex".
        login.succeeded = true;
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

void DemoApp::confirmsignuplink2_result(handle, const char *name, const char *email, error e)
{
    if (e)
    {
        cout << "Signuplink confirmation failed (" << errorstring(e) << ")" << endl;
    }
    else
    {
        cout << "Signup confirmed successfully. Logging by first time..." << endl;
        login.reset();
        login.email = email;
        login.password = newpassword;
        client->prelogin(email);
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
        if (!uk.setkey(AsymmCipher::PRIVKEY, privkbuf, static_cast<int>(len_privk)))
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
    if (client->loggedin() == EPHEMERALACCOUNT)
    {
        cout << Base64Str<MegaClient::USERHANDLE>(uh) << "#";
        cout << Base64Str<SymmCipher::KEYLENGTH>(pw) << endl;
    }
    else
    {
        string session;
        client->dumpsession(session);
        cout << Base64::btoa(session) << endl;
    }

    client->fetchnodes(false, false, false);
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
            reason = "Your account has been suspended due to copyright violations. Please check your email inbox.";
        }
        else if (code == 300)
        {
            reason = "Your account was terminated due to a breach of MEGA's Terms of Service, such as abuse of rights of others; sharing and/or importing illegal data; or system abuse.";
        }
        else if (code == 400)
        {
            reason = "Your account has been disabled by your administrator. You may contact your business account administrator for further details.";
        }
        else if (code == 401)
        {
            reason = "Your account has been removed by your administrator. You may contact your business account administrator for further details.";
        }
        else if (code == 500)
        {
            reason = "Your account has been blocked pending verification via SMS.";
        }
        else if (code == 700)
        {
            reason = "Your account has been temporarily suspended for your safety. Please verify your email and follow its steps to unlock your account.";
        }
        //else if (code == ACCOUNT_BLOCKED_DEFAULT) --> default reason

        cout << "Reason: " << reason << endl;

        if (code != 500 && code != 700)
        {
            cout << "Logging out..." << endl;
            client->locallogout(true, true);
        }
    }
}

void DemoApp::upgrading_security()
{
    cout << "We are upgrading the cryptographic resilience of your account. You will see this message only once. "
         << "If you see it again in the future, you may be under attack by us. If you have seen it in the past, do not proceed." << endl;

    cout << "You are currently sharing the following folders." << endl;
    listallshares();
    cout << "------------------------------------------------" << endl;

    client->upgradeSecurity([](Error e) {
        if (e)
        {
            cout << "Security upgrade failed (" << errorstring(e) << ")" << endl;
            exit(1);
        }
        else
        {
            cout << "Security upgrade succeeded." << endl;
        }
    });
}

void DemoApp::downgrade_attack()
{
    cout << "A downgrade attack has been detected. Removed shares may have reappeared. Please tread carefully.";
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


void exportnode_result(Error e, handle h, handle ph)
{
    if (e)
    {
        cout << "Export failed: " << errorstring(e) << endl;
        return;
    }

    std::shared_ptr<Node> n;

    if ((n = client->nodebyhandle(h)))
    {
        string path;
        nodepath(NodeHandle().set6byte(h), &path);
        cout << "Exported " << path << ": ";

        if (n->type != FILENODE && !n->sharekey)
        {
            cout << "No key available for exported folder" << endl;
            return;
        }

        string publicLink;
        TypeOfLink lType = client->validTypeForPublicURL(n->type);
        if (n->type == FILENODE)
        {
            publicLink = MegaClient::publicLinkURL(client->mNewLinkFormat, lType, ph, Base64Str<FILENODEKEYLENGTH>((const byte*)n->nodekey().data()));
        }
        else
        {
            publicLink = MegaClient::publicLinkURL(client->mNewLinkFormat, lType, ph, Base64Str<FOLDERNODEKEYLENGTH>(n->sharekey->key));
        }

        cout << publicLink;

        if (n->plink)
        {
            string authKey = n->plink->mAuthKey;

            if (authKey.size())
            {
                string authToken(publicLink);
                authToken = authToken.substr(MegaClient::MEGAURL.size()+strlen("/folder/")).append(":").append(authKey);
                cout << "\n          AuthToken = " << authToken;
            }
        }

        cout << endl;

    }
    else
    {
        cout << "Exported node no longer available" << endl;
    }
}

// the requested link could not be opened
void DemoApp::openfilelink_result(const Error& e)
{
    if (e)
    {
        if (pdf_to_import) // import welcome pdf has failed
        {
            cout << "Failed to import Welcome PDF file" << endl;
        }
        else
        {
            if (e == API_ETOOMANY && e.hasExtraInfo())
            {
                cout << "Failed to open link: " << getExtraInfoErrorString(e) << endl;
            }
            else
            {
                cout << "Failed to open link: " << errorstring(e) << endl;
            }

        }
    }
    pdf_to_import = false;
}

// the requested link was opened successfully - import to cwd
void DemoApp::openfilelink_result(handle ph, const byte* key, m_off_t size,
                                  string* a, string* /*fa*/, int)
{
    std::shared_ptr<Node> n;

    if (!key)
    {
        cout << "File is valid, but no key was provided." << endl;
        pdf_to_import = false;
        return;
    }

    // check if the file is decryptable
    string attrstring;

    attrstring.resize(a->length()*4/3+4);
    attrstring.resize(static_cast<size_t>(
        Base64::btoa((const byte*)a->data(), int(a->length()), (char*)attrstring.data())));

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
            n = client->nodeByHandle(client->mNodeManager.getRootNodeFiles());
        }
        else
        {
            n = client->nodeByHandle(cwd);
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
        vector<NewNode> nn(1);
        NewNode* newnode = &nn[0];

        // set up new node as folder node
        newnode->source = NEW_PUBLIC;
        newnode->type = FILENODE;
        newnode->nodehandle = ph;
        newnode->parenthandle = UNDEF;
        newnode->nodekey.assign((char*)key, FILENODEKEYLENGTH);
        newnode->attrstring.reset(new string(*a));

        while ((name = json.getnameid()) != EOO && json.storeobject((t = &attrs.map[name])))
        {
            JSON::unescape(t);

            if (name == 'n')
            {
                LocalPath::utf8_normalize(t);
            }
        }

        attr_map::iterator it = attrs.map.find('n');
        if (it != attrs.map.end())
        {
            std::shared_ptr<Node> ovn = client->childnodebyname(n.get(), it->second.c_str(), true);
            if (ovn)
            {
                attr_map::iterator it2 = attrs.map.find('c');
                if (it2 != attrs.map.end())
                {
                    FileFingerprint ffp;
                    if (ffp.unserializefingerprint(&it2->second))
                    {
                        ffp.size = size;
                        if (ffp.isvalid && ovn->isvalid && ffp == *(FileFingerprint*)ovn.get())
                        {
                            cout << "Success. (identical node skipped)" << endl;
                            pdf_to_import = false;
                            delete [] buf;
                            return;
                        }
                    }
                }

                newnode->ovhandle = ovn->nodeHandle();
            }
        }

        client->putnodes(n->nodeHandle(), UseLocalVersioningFlag, std::move(nn), nullptr, client->restag, false);
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
    byte folderkey[FOLDERNODEKEYLENGTH];
    #ifndef NDEBUG
    error eaux =
    #endif
    client->parsepubliclink(publiclink.c_str(), ph, folderkey, TypeOfLink::FOLDER);
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
            string fingerprint; // raw fingerprint without App's prefix (different layer)
            FileFingerprint ffp;
            m_time_t mtime = 0;
            Node::parseattr(buf, attrs, currentSize, mtime, fileName, fingerprint, ffp);

            // Normalize node name to UTF-8 string
            attr_map::iterator it = attrs.map.find('n');
            if (it != attrs.map.end() && !it->second.empty())
            {
                LocalPath::utf8_normalize(&(it->second));
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

bool DemoApp::pread_data(byte* data, m_off_t len, m_off_t pos, m_off_t, m_off_t, void* /*appdata*/)
{
    // Improvement: is there a way to have different pread_data receivers for
    // different modes?
    if(more_node)  // are we paginating through a node?
    {
        fwrite(data, 1, size_t(len), stdout);
        if((pos + len) >= more_node->size) // is this the last chunk?
        {
            more_node = nullptr;
            more_offset = 0;
            cout << "-End of file-" << endl;
            setprompt(COMMAND);
        }
        else
        {
            // there's more to get, so set PAGER prompt
            setprompt(PAGER);
            more_offset += len;
        }
    }
    else if (pread_file)
    {
        pread_file->write((const char*)data, static_cast<std::streamsize>(len));
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

dstime DemoApp::pread_failure(const Error &e, int retry, void* /*appdata*/, dstime)
{
    if (retry < 5 && !(e == API_ETOOMANY && e.hasExtraInfo()))
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
        return NEVER;
    }
}

// reload needed
void DemoApp::notifyError(const char* reason, ErrorReason errorReason)
{
    cout << "Error has been detected: " << errorReason << " (" << reason << ")" << endl;
}

void DemoApp::reloading()
{
    cout << "Reload forced from server. Waiting for response..." << endl;
}

// reload initiated
void DemoApp::clearing()
{
    LOG_debug << "Clearing all nodes/users...";
}

// nodes have been modified
// (nodes with their removed flag set will be deleted immediately after returning from this call,
// at which point their pointers will become invalid at that point.)
void DemoApp::nodes_updated(sharedNode_vector* nodes, int count)
{
    int c[2][6] = { { 0 } };

    if (nodes)
    {
        auto it = nodes->begin();
        while (count--)
        {
            if ((*it)->type < 6)
            {
                c[!(*it)->changed.removed][(*it)->type]++;
                it++;
            }
        }
    }
    else
    {
        sharedNode_vector rootNodes = client->mNodeManager.getRootNodes();

        sharedNode_vector inshares = client->mNodeManager.getNodesWithInShares();
        rootNodes.insert(rootNodes.end(), inshares.begin(), inshares.end());
        for (auto& node : rootNodes)
        {
            if (!node->parent) // No take account nested inshares
            {
                c[1][node->type] ++;
                c[1][FOLDERNODE] += static_cast<int>(node->getCounter().folders);
                c[1][FILENODE] += static_cast<int>(node->getCounter().files + node->getCounter().versions);
            }
        }
    }

    nodestats(c[1], "added or updated");
    nodestats(c[0], "removed");

    if (cwd.isUndef())
    {
        cwd = client->mNodeManager.getRootNodeFiles();
    }
}

// nodes now (almost) current, i.e. no server-client notifications pending
void DemoApp::nodes_current()
{
    LOG_debug << "Nodes current.";
}

void DemoApp::account_updated()
{
    if (client->loggedin() == EPHEMERALACCOUNT || client->loggedin() == EPHEMERALACCOUNTPLUSPLUS)
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
    if (client->loggedin() == EPHEMERALACCOUNT || client->loggedin() == EPHEMERALACCOUNTPLUSPLUS)
    {
        LOG_debug << "Account has been confirmed with email " << email << ".";
    }
}

void DemoApp::notify_confirm_user_email(handle user, const char *email)
{
    if (client->loggedin() == EPHEMERALACCOUNT || client->loggedin() == EPHEMERALACCOUNTPLUSPLUS)
    {
        LOG_debug << "Account has been confirmed with user " << toHandle(user) << " and email " << email << ". Proceed to login with credentials.";
        cout << "Account has been confirmed with user " << toHandle(user) << " and email " << email << ". Proceed to login with credentials." << endl;
    }
}

void DemoApp::sequencetag_update(const string& st)
{
    if(gVerboseMode)
    {
        conlock(cout) << "Latest seqTag: " << st << endl;
    }
}

// set addition/update/removal/export {en|dis}able/
void DemoApp::sets_updated(Set** s, int count)
{
    cout << (count == 1 ? string("1 Set") : (std::to_string(count) + " Sets")) << " received" << endl;

    if (!s) return;

    for (int i = 0; i < count; i++)
    {
        Set* set = s[i];
        cout << "Set " << toHandle(set->id());
        if (set->hasChanged(Set::CH_NEW))
        {
            cout << " has been added";
        }
        if (set->hasChanged(Set::CH_EXPORTED))
        {
            cout << " export has been " << (set->publicId() == UNDEF ? "dis" : "en") << "abled";
        }
        else if (set->hasChanged(Set::CH_REMOVED))
        {
            cout << " has been removed";
        }
        else
        {
            if (set->hasChanged(Set::CH_NAME))
            {
                cout << endl << "\tchanged name";
            }
            if (set->hasChanged(Set::CH_COVER))
            {
                cout << endl << "\tchanged cover";
            }
        }
        cout << endl;
    }
}

// element addition/update/removal
void DemoApp::setelements_updated(SetElement** el, int count)
{
    cout << (count == 1 ? string("1 Element") : (std::to_string(count) + " Elements")) << " received" << endl;

    if (!el) return;

    for (int i = 0; i < count; i++)
    {
        SetElement* elem = el[i];
        cout << "Element " << toHandle(elem->id());
        if (elem->hasChanged(SetElement::CH_EL_NEW))
        {
            cout << " has been added";
        }
        else if (elem->hasChanged(Set::CH_REMOVED))
        {
            cout << " has been removed";
        }
        else
        {
            if (elem->hasChanged(SetElement::CH_EL_NAME))
            {
                cout << endl << "\tchanged name";
            }
            if (elem->hasChanged(SetElement::CH_EL_ORDER))
            {
                cout << endl << "\tchanged order";
            }
        }
        cout << endl;
    }
}

void DemoApp::enumeratequotaitems_result(const Product& product)
{
    if (product.planType != 1) // All plans but Business
    {
        cout << "\n" << product.description << ":\n";
        cout << "\tPro level: " << product.proLevel << "\n";
        cout << "\tStorage: " << product.gbStorage << "\n";
        cout << "\tTransfer: " << product.gbTransfer << "\n";
        cout << "\tMonths: " << product.months << "\n";
        cout << "\tAmount: " << product.amount << "\n";
        cout << "\tAmount per month: " << product.amountMonth << "\n";
        cout << "\tLocal price: " << product.localPrice << "\n";
        cout << "\tFeatures:\n";
        if (product.features.empty())
        {
            cout << "\t\t[none]\n";
        }
        else
        {
            for (const auto& f: product.features)
            {
                cout << "\t\t" << f.first << ": " << f.second << '\n';
            }
        }
        cout << "\tiOS ID: " << product.iosid << "\n";
        cout << "\tAndroid ID: " << product.androidid << "\n";
        cout << "\tTest Category: " << product.testCategory << "\n";
        cout << "\tTrial Days: " << product.trialDays << endl;
    }
    else // Business plan (type == 1)
    {
        cout << "\n" << product.description << ":\n";
        cout << "\tMinimum users: " << product.businessPlan->minUsers << "\n";
        cout << "\tStorage per user: " << product.businessPlan->gbStoragePerUser << "\n";
        cout << "\tTransfer per user: " << product.businessPlan->gbTransferPerUser << "\n";
        cout << "\tPrice per user: " << product.businessPlan->pricePerUser << "\n";
        cout << "\tLocal price per user: " << product.businessPlan->localPricePerUser << "\n";
        cout << "\tPrice per storage: " << product.businessPlan->pricePerStorage << "\n";
        cout << "\tLocal price per storage: " << product.businessPlan->localPricePerStorage << "\n";
        cout << "\tGigabytes per storage: " << product.businessPlan->gbPerStorage << "\n";
        cout << "\tPrice per transfer: " << product.businessPlan->pricePerTransfer << "\n";
        cout << "\tLocal price per transfer: " << product.businessPlan->localPricePerTransfer
             << "\n";
        cout << "\tGigabytes per transfer: " << product.businessPlan->gbPerTransfer << "\n";
        cout << "\tTest Category: " << product.testCategory << endl;
    }
}

void DemoApp::enumeratequotaitems_result(unique_ptr<CurrencyData> data)
{
    cout << "\nCurrency data: " << endl;
    cout << "\tName: " << data->currencyName;
    cout << "\tSymbol: " << Base64::atob(data->currencySymbol);
    if (data->localCurrencyName.size())
    {
        cout << "\tName (local): " << data->localCurrencyName;
        cout << "\tSymbol (local): " << Base64::atob(data->localCurrencySymbol);
    }
    cout << endl;
}

void DemoApp::enumeratequotaitems_result(error e)
{
    if (e != API_OK)
    {
        cout << "Error retrieving pricing plans, error code " << e << endl;
    }
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
        cout << "\tFirstname: " << Base64::atob(*fn) << endl;
        cout << "\tLastname: " << Base64::atob(*ln) << endl;
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

void reportNodeStorage(NodeStorage* ns, const string& rootnodename)
{
    cout << "\t\tIn " << rootnodename << ": " << ns->bytes << " byte(s) in " << ns->files << " file(s) and " << ns->folders << " folder(s)" << endl;
    cout << "\t\tUsed storage by versions: " << ns->version_bytes << " byte(s) in " << ns->version_files << " file(s)" << endl;
}

// display account details/history
void DemoApp::account_details(AccountDetails* ad, bool storage, bool transfer, bool pro, bool purchases,
                              bool transactions, bool sessions)
{
    char timebuf[32], timebuf2[32];

    if (storage)
    {
        cout << "\tAvailable storage: " << ad->storage_max << " byte(s)  used:  " << ad->storage_used << " available: " << (ad->storage_max - ad->storage_used) << endl;

        reportNodeStorage(&ad->storage[client->mNodeManager.getRootNodeFiles().as8byte()], "/");
        reportNodeStorage(&ad->storage[client->mNodeManager.getRootNodeVault().as8byte()], "//in");
        reportNodeStorage(&ad->storage[client->mNodeManager.getRootNodeRubbish().as8byte()], "//bin");
    }

    if (transfer)
    {
        if (ad->transfer_max)
        {
            long long transferFreeUsed = 0;
            for (unsigned i = 0; i < ad->transfer_hist.size(); i++)
            {
                transferFreeUsed += ad->transfer_hist[i];
            }

            cout << "\tTransfer in progress: " << ad->transfer_own_reserved << "/" << ad->transfer_srv_reserved << endl;
            cout << "\tTransfer completed: " << ad->transfer_own_used << "/" << ad->transfer_srv_used << "/" << transferFreeUsed << " of "
                 << ad->transfer_max << " ("
                 << (100 * (ad->transfer_own_used + ad->transfer_srv_used + transferFreeUsed) / ad->transfer_max) << "%)" << endl;
            cout << "\tServing bandwidth ratio: " << ad->srv_ratio << "%" << endl;
        }

        if (ad->transfer_hist_starttime)
        {
            m_time_t t = m_time() - ad->transfer_hist_starttime;

            cout << "\tTransfer history:\n";

            for (unsigned i = 0; i < ad->transfer_hist.size(); i++)
            {
                cout << "\t\t" << t;
                t -= ad->transfer_hist_interval;
                if (t < 0)
                {
                    cout << " second(s) ago until now: ";
                }
                else
                {
                    cout << "-" << t << " second(s) ago: ";
                }
                cout << ad->transfer_hist[i] << " byte(s)" << endl;
            }
        }
    }

    if (pro)
    {
        cout << "\tAccount Subscriptions:" << endl;
        for (const auto& sub: ad->subscriptions)
        {
            cout << "\t\t* ID: " << sub.id << endl;
            cout << "\t\t\t Status(type): ";
            switch (sub.type)
            {
                case 'S':
                    cout << "VALID";
                    break;
                case 'R':
                    cout << "INVALID";
                    break;
                default:
                    cout << "NONE";
                    break;
            }
            cout << " (" << sub.type << ")" << endl;
            cout << "\t\t\t Cycle: " << sub.cycle << endl;
            cout << "\t\t\t Payment Method: " << sub.paymentMethod << endl;
            cout << "\t\t\t Payment Method ID: " << sub.paymentMethodId << endl;
            cout << "\t\t\t Renew time: " << sub.renew << endl;
            cout << "\t\t\t Account level: " << sub.level << endl;
            cout << "\t\t\t Is Trial: " << (sub.isTrial ? "Yes" : "No") << endl;
            cout << "\t\t\t Features: ";
            for (const auto& f: sub.features)
            {
                cout << f << ", ";
            }
            cout << endl;
        }

        cout << "\tAccount Plans:" << endl;
        for (const auto& plan: ad->plans)
        {
            cout << "\t\t* Plan details: " << endl;
            cout << "\t\t\t Account level: " << plan.level << endl;
            cout << "\t\t\t Is Trial: " << (plan.isTrial ? "Yes" : "No") << endl;
            cout << "\t\t\t Features: ";
            for (const auto& f: plan.features)
            {
                cout << f << ", ";
            }
            cout << endl;
            cout << "\t\t\t Expiration time: " << plan.expiration << endl;
            cout << "\t\t\t Plan type: " << plan.type << endl;
            cout << "\t\t\t Related subscription id: " << plan.subscriptionId << endl;
        }

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
                printf("\tSession ID: %s\n\tSession start: %s\n\tMost recent activity: %s\n\tIP: %s\n\tCountry: %.2s\n\tUser-Agent: %s\n\tDevice ID: %s\n\t-----\n",
                        id.chars, timebuf, timebuf2, it->ip.c_str(), it->country, it->useragent.c_str(), it->deviceid.c_str());
            }
        }

        if(gVerboseMode)
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

void DemoApp::resetSmsVerifiedPhoneNumber_result(error e)
{
    if (e)
    {
        cout << "Reset verified phone number failed: " << e << endl;
    }
    else
    {
        cout << "Reset verified phone number succeeded" << endl;
    }
}

void DemoApp::getbanners_result(error e)
{
    cout << "Getting Smart Banners failed: " << e << endl;
}

void DemoApp::getbanners_result(vector< tuple<int, string, string, string, string, string, string> >&& banners)
{
    for (auto& b : banners)
    {
        cout << "Smart Banner:" << endl
             << "\tid         : " << std::get<0>(b) << endl
             << "\ttitle      : " << std::get<1>(b) << endl
             << "\tdescription: " << std::get<2>(b) << endl
             << "\timage      : " << std::get<3>(b) << endl
             << "\turl        : " << std::get<4>(b) << endl
             << "\tbkgr image : " << std::get<5>(b) << endl
             << "\tdsp        : " << std::get<6>(b) << endl;
    }
}

void DemoApp::dismissbanner_result(error e)
{
    if (e)
    {
        cout << "Dismissing Smart Banner failed: " << e << endl;
    }
    else
    {
        cout << "Dismissing Smart Banner succeeded" << endl;
    }
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

char** my_rl_completion(const char* /*text*/, int /*start*/, int end)
{
    rl_attempted_completion_over = 1;

    std::string line(rl_line_buffer, static_cast<size_t>(end));
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

    // Initialize history.
    using_history();

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
            ostringstream  dynamicprompt;

            // display put/get transfer speed in the prompt
            if (client->tslots.size() || responseprogress >= 0)
            {
                m_off_t xferrate[2] = { 0 };
                Waiter::bumpds();

                for (transferslot_list::iterator it = client->tslots.begin(); it != client->tslots.end(); it++)
                {
                    if ((*it)->fa)
                    {
                        xferrate[(*it)->transfer->type]
                            += (*it)->mTransferSpeed.getCircularMeanSpeed();
                    }
                }
                xferrate[GET] /= 1024;
                xferrate[PUT] /= 1024;

                dynamicprompt << "MEGA";

                if (xferrate[GET] || xferrate[PUT] || responseprogress >= 0)
                {
                    dynamicprompt << " (";

                    if (xferrate[GET])
                    {
                        dynamicprompt << "In: " << xferrate[GET] << " KB/s";

                        if (xferrate[PUT])
                        {
                            dynamicprompt << "/";
                        }
                    }

                    if (xferrate[PUT])
                    {
                        dynamicprompt << "Out: " << xferrate[PUT] << " KB/s";
                    }

                    if (responseprogress >= 0)
                    {
                        dynamicprompt << responseprogress << "%";
                    }

                    dynamicprompt  << ")";
                }

                dynamicprompt  << "> ";
            }

            string dynamicpromptstr = dynamicprompt.str();

#if defined(WIN32) && defined(NO_READLINE)
            {
                auto cl = conlock(cout);
                static_cast<WinConsole*>(console)->updateInputPrompt(!dynamicpromptstr.empty() ? dynamicpromptstr : prompts[COMMAND]);
            }
#else
            rl_callback_handler_install(!dynamicpromptstr.empty() ? dynamicpromptstr.c_str() : prompts[prompt], store_line);

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
                if ((prompt == COMMAND) || (prompt == PAGER))
                {
                    // Note: this doesn't act like unbuffered input, still
                    // requires Return line ending
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
            else if (prompt != COMMAND)
            {
                setprompt(prompt);
            }
            free(line);
            line = NULL;

            if (quit_flag)
            {
#ifndef NO_READLINE
                rl_callback_handler_remove();
#endif /* ! NO_READLINE */
                delete client;
                client = nullptr;
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


        auto puts = appxferq[PUT].size();
        auto gets = appxferq[GET].size();

        // pass the CPU to the engine (nonblocking)
        client->exec();
        if (clientFolder) clientFolder->exec();

        if (login.succeeded)
        {
            // Continue with fetchnodes
            login.fetchnodes(client);
            client->exec();
        }

        if (puts && !appxferq[PUT].size())
        {
            cout << "Uploads complete" << endl;
            if (onCompletedUploads) onCompletedUploads();
        }
        if (gets && !appxferq[GET].size())
        {
            cout << "Downloads complete" << endl;
        }

        while (!mainloopActions.empty())
        {
            mainloopActions.front()();
            mainloopActions.pop_front();
        }

    }
}

#ifndef NO_READLINE

static void onFatalSignal(int signum)
{
    // Restore the terminal's settings.
    rl_callback_handler_remove();

    // Re-trigger the signal.
    raise(signum);
}

static void registerSignalHandlers()
{
    std::vector<int> signals = {
        SIGABRT,
        SIGBUS,
        SIGILL,
        SIGKILL,
        SIGSEGV,
        SIGTERM
    }; // signals

    struct sigaction action;

    action.sa_handler = &onFatalSignal;

    // Restore default signal handler after invoking our own.
    action.sa_flags = static_cast<int>(SA_NODEFER | SA_RESETHAND);

    // Don't ignore any signals.
    sigemptyset(&action.sa_mask);

    for (int signal : signals)
    {
        (void)sigaction(signal, &action, nullptr);
    }
}

#endif // ! NO_READLINE

MegaClient::ClientType getClientTypeFromArgs(const std::string& clientType)
{
    if (clientType == "vpn")
    {
        return MegaClient::ClientType::VPN;
    }
    if (clientType == "password_manager")
    {
        return MegaClient::ClientType::PASSWORD_MANAGER;
    }
    if (clientType != "default")
    {
        cout << "WARNING: Invalid argument " << clientType
             << ". Valid possibilities are: vpn, password_manager, default.\nUsing default instead."
             << endl;
    }

    return MegaClient::ClientType::DEFAULT;
}

int main(int argc, char* argv[])
{
#if defined(_WIN32) && defined(_DEBUG)
    _CrtSetBreakAlloc(124);  // set this to an allocation number to hunt leaks.  Prior to 124 and prior are from globals/statics so won't be detected by this
#endif

#ifndef NO_READLINE
    registerSignalHandlers();
#endif // NO_READLINE

    auto arguments = ArgumentsParser::parse(argc, argv);

    if (arguments.contains("-h"))
    {
        cout << USAGE << endl;
        return 0;
    }

    if (arguments.contains("-v"))
    {
        cout << "Arguments: \n"
             << arguments;
    }

    // config from arguments
    Config config;
    try
    {
        config = Config::fromArguments(arguments);
    }
    catch(const std::exception& e)
    {
        cout << "Error: " << e.what() << endl;
        cout << USAGE << endl;
        return -1;
    }

    SimpleLogger::setLogLevel(logMax);
    auto gLoggerAddr = &gLogger;
    g_externalLogger.addMegaLogger(&gLogger,

        [gLoggerAddr](const char *time, int loglevel, const char *source, const char *message
#ifdef ENABLE_LOG_PERFORMANCE
            , const char **directMessages, size_t *directMessagesSizes, unsigned numberMessages
#endif
            ){
                gLoggerAddr->log(time, loglevel, source, message
#ifdef ENABLE_LOG_PERFORMANCE
                    , directMessages, directMessagesSizes, numberMessages
#endif
                );
         });

    console = new CONSOLE_CLASS;

    std::unique_ptr<IGfxProvider> provider = createGfxProvider(config);
    mega::GfxProc* gfx = provider ? new GfxProc(std::move(provider)) : nullptr;
    if (gfx) gfx->startProcessingThread();

    // Needed so we can get the cwd.
    auto fsAccess = std::make_unique<FSACCESS_CLASS>();

#ifdef __APPLE__
    // Try and raise the file descriptor limit as high as we can.
    platformSetRLimitNumFile();
#endif

    // Where are we?
    if (!fsAccess->cwd(*startDir))
    {
        cerr << "Unable to determine current working directory." << endl;
        return EXIT_FAILURE;
    }

    fsAccess.reset();

    auto httpIO = new CurlHttpIO;

#ifdef WIN32
    auto waiter = std::make_shared<CONSOLE_WAIT_CLASS>(static_cast<CONSOLE_CLASS*>(console));
#else
    auto waiter = std::make_shared<CONSOLE_WAIT_CLASS>();
#endif

    auto demoApp = new DemoApp;

    auto dbAccess =
#ifdef DBACCESS_CLASS
        new DBACCESS_CLASS(*startDir);
#else
        nullptr;
#endif

    auto clientType = getClientTypeFromArgs(config.clientType);

    // instantiate app components: the callback processor (DemoApp),
    // the HTTP I/O engine and the MegaClient itself
    client = new MegaClient(demoApp,
                            waiter,
                            httpIO,
                            dbAccess,
                            gfx,
                            "Gk8DyQBS",
                            "megacli/" TOSTRING(MEGA_MAJOR_VERSION)
                            "." TOSTRING(MEGA_MINOR_VERSION)
                            "." TOSTRING(MEGA_MICRO_VERSION),
                            2,
                            clientType);

    ac::ACN acs = autocompleteSyntax();
#if defined(WIN32) && defined(NO_READLINE)
    static_cast<WinConsole*>(console)->setAutocompleteSyntax((acs));
#endif

    clientFolder = NULL;    // additional for folder links

    megacli();

    delete client;
    delete httpIO;
    delete gfx;
    delete demoApp;
    acs.reset();
    autocompleteTemplate.reset();
    delete console;
    startDir.reset();

#if defined(USE_OPENSSL) && !defined(OPENSSL_IS_BORINGSSL)
    if (CurlHttpIO::sslMutexes)
    {
        int numLocks = CRYPTO_num_locks();
        for (int i = 0; i < numLocks; ++i)
        {
            delete CurlHttpIO::sslMutexes[i];
        }
        delete [] CurlHttpIO::sslMutexes;
    }
#endif

#if defined(_WIN32) && defined(_DEBUG)

    // Singleton enthusiasts rarely think about shutdown...
    const CryptoPP::MicrosoftCryptoProvider &hProvider = CryptoPP::Singleton<CryptoPP::MicrosoftCryptoProvider>().Ref();
    delete &hProvider;

    _CrtDumpMemoryLeaks();
#endif
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
        clientFolder->fetchnodes(false, true, false);
    }
}

void DemoAppFolder::fetchnodes_result(const Error& e)
{
    bool success = false;
    if (e)
    {
        if (e == API_ENOENT && e.hasExtraInfo())
        {
            cout << "Folder retrieval failed: " << getExtraInfoErrorString(e) << endl;
        }
        else
        {
            cout << "Folder retrieval failed (" << errorstring(e) << ")" << endl;
        }
    }
    else
    {
        // check if the key is invalid
        if (clientFolder->isValidFolderLink())
        {
            cout << "Folder link loaded correctly." << endl;
            success = true;
        }
        else
        {
            assert(client->nodeByHandle(client->mNodeManager.getRootNodeFiles()));   // node is there, but cannot be decrypted
            cout << "Folder retrieval succeed, but encryption key is wrong." << endl;
        }
    }

    if (!success)
    {
        delete clientFolder;
        clientFolder = NULL;
    }
}

void DemoAppFolder::nodes_updated(sharedNode_vector* nodes, int count)
{
    int c[2][6] = { { 0 } };

    if (nodes)
    {
        auto it = nodes->begin();
        while (count--)
        {
            if ((*it)->type < 6)
            {
                c[!(*it)->changed.removed][(*it)->type]++;
                it++;
            }
        }
    }
    else
    {
        sharedNode_vector rootNodes = client->mNodeManager.getRootNodes();
        for (auto& node : rootNodes)
        {
            c[1][node->type] ++;
            c[1][FOLDERNODE] += static_cast<int>(node->getCounter().folders);
            c[1][FILENODE] += static_cast<int>(node->getCounter().files + node->getCounter().versions);
        }
    }

    cout << "The folder link contains ";
    nodestats(c[1], "");
}

void exec_metamac(autocomplete::ACState& s)
{
    std::shared_ptr<Node> node = nodebypath(s.words[2].s.c_str());
    if (!node || node->type != FILENODE)
    {
        cerr << s.words[2].s
             << (node ? ": No such file or directory"
                      : ": Not a file")
             << endl;
        return;
    }

    auto ifAccess = client->fsaccess->newfileaccess();
    {
        auto localPath = localPathArg(s.words[1].s);
        if (!ifAccess->fopen(localPath, 1, 0, FSLogging::logOnError))
        {
            cerr << "Failed to open: " << s.words[1].s << endl;
            return;
        }
    }

    SymmCipher cipher;
    int64_t remoteIv;
    int64_t remoteMac;

    {
        std::string remoteKey = node->nodekey();
        const char *iva = &remoteKey[SymmCipher::KEYLENGTH];

        cipher.setkey((byte*)&remoteKey[0], node->type);
        remoteIv = MemAccess::get<int64_t>(iva);
        remoteMac = MemAccess::get<int64_t>(iva + sizeof(int64_t));
    }

    auto result = generateMetaMac(cipher, *ifAccess, remoteIv);
    if (!result.first)
    {
        cerr << "Failed to generate metamac for: "
             << s.words[1].s
             << endl;
    }
    else
    {
        const std::ios::fmtflags flags = cout.flags();

        cout << s.words[2].s
             << " (remote): "
             << std::hex
             << (uint64_t)remoteMac
             << "\n"
             << s.words[1].s
             << " (local): "
             << (uint64_t)result.second
             << endl;

        cout.flags(flags);
    }
}

void exec_resetverifiedphonenumber(autocomplete::ACState& s)
{
    client->resetSmsVerifiedPhoneNumber();
}

void exec_banner(autocomplete::ACState& s)
{
    if (s.words.size() == 2 && s.words[1].s == "get")
    {
        cout << "Retrieving banner info..." << endl;

        client->reqs.add(new CommandGetBanners(client));
    }
    else if (s.words.size() == 3 && s.words[1].s == "dismiss")
    {
        cout << "Dismissing banner with id " << s.words[2].s << "..." << endl;

        client->reqs.add(new CommandDismissBanner(client, stoi(s.words[2].s), m_time(nullptr)));
    }
}

#ifdef ENABLE_SYNC

void sync_completion(error result, const SyncError& se, handle backupId)
{
    if (backupId == UNDEF)
    {
        cerr << "Sync could not be added " << (se == SyncError::PUT_NODES_ERROR ? "(putnodes for backup failed)" : "") << ": "
             << errorstring(result)
             << endl;
    }
    else if (result == API_OK && se == NO_SYNC_ERROR)
    {
        cerr << "Sync added and running: "
             << toHandle(backupId)
             << endl;
    }
    else
    {
        cerr << "Sync added but could not be started: "
             << errorstring(result)
             << endl;
    }
}

void exec_syncadd(autocomplete::ACState& s)
{
    if (client->loggedin() != FULLACCOUNT)
    {
        cerr << "You must be logged in to create a sync."
             << endl;
        return;
    }

    string drive, syncname, scanInterval;
    bool backup = s.extractflag("-backup");
    bool external = s.extractflagparam("-external", drive);
    bool named = s.extractflagparam("-name", syncname);
    bool scanOnly = s.extractflag("-scan-only");
    bool scanIntervalSpecified = s.extractflagparam("-scan-interval", scanInterval);
    LocalPath sourcePath = localPathArg(s.words[2].s);

    if (!named)
    {
        syncname = sourcePath.leafOrParentName();
    }

    // sync add source target
    LocalPath drivePath = external ? localPathArg(drive) : LocalPath();

    // Create a suitable sync config.
    auto config =
        SyncConfig(sourcePath,
            syncname,
            NodeHandle(),
            string(),
            fsfp_t(),
            std::move(drivePath),
            true,
            backup ? SyncConfig::TYPE_BACKUP : SyncConfig::TYPE_TWOWAY);


    // Scan interval
    if (scanIntervalSpecified)
    {
        auto i = atoi(scanInterval.c_str());

        if (i >= 0)
            config.mScanIntervalSec = static_cast<unsigned>(i);
    }

    // Scan only.
    if (scanOnly)
        config.mChangeDetectionMethod = CDM_PERIODIC_SCANNING;


    if (!backup) // regular sync
    {
        // Does the target node exist?
        const string& targetPath = s.words[3].s;
        std::shared_ptr<Node> targetNode = nodebypath(targetPath.c_str());

        if (!targetNode)
        {
            cerr << targetPath
                << ": Not found."
                << endl;
            return;
        }

        config.mRemoteNode = targetNode ? NodeHandle().set6byte(targetNode->nodehandle) : NodeHandle();
        config.mOriginalPathOfRemoteRootNode = targetNode ? targetNode->displaypath() : string();

        client->addsync(std::move(config), sync_completion, "", "");
    }

    else // backup
    {
        // drive must be without trailing separator
        if (!drive.empty() && drive.back() == LocalPath::localPathSeparator_utf8)
        {
            drive.pop_back();
        }

#ifdef _WIN32
        // source can be with or without trailing separator, except for Win where it must have it for drive root (OMG...)
        string src = s.words[2].s;
        if (!src.empty() && src.back() != LocalPath::localPathSeparator_utf8)
        {
            src.push_back(LocalPath::localPathSeparator_utf8);
            sourcePath = LocalPath::fromAbsolutePath(src);
        }
#endif

        client->preparebackup(config, [](Error err, SyncConfig sc, MegaClient::UndoFunction revertOnError){

            if (err != API_OK)
            {
                sync_completion(error(err), SyncError::PUT_NODES_ERROR, UNDEF);
            }
            else
            {
                client->addsync(std::move(sc), [revertOnError](error e, SyncError se, handle h){

                    if (e != API_OK)
                    {
                        if (revertOnError)
                        {
                            cerr << "Removing the created backup node, as backup sync add failed" << endl;
                            revertOnError(nullptr);
                        }
                    }
                    sync_completion(e, se, h);

                }, "", "");
            }
        });
    }
}

void exec_syncrename(autocomplete::ACState& s)
{
    // Are we logged in?
    if (client->loggedin() != FULLACCOUNT)
    {
        cerr << "You must be logged in to manipulate backup syncs."
            << endl;
        return;
    }

    // get id
    handle backupId = 0;
    Base64::atob(s.words[2].s.c_str(), (byte*) &backupId, sizeof(handle));

    string newname = s.words[3].s;

    client->syncs.renameSync(backupId, newname,
        [&](Error e)
        {
            if (!e) cout << "Rename succeeded" << endl;
            else cout << "Rename failed: " << e << endl;
        });
}

void exec_syncclosedrive(autocomplete::ACState& s)
{
    // Are we logged in?
    if (client->loggedin() != FULLACCOUNT)
    {
        cerr << "You must be logged in to manipulate backup syncs."
             << endl;
        return;
    }

    // sync backup remove drive
    const auto drivePath =
        localPathArg(s.words[2].s);

    client->syncs.backupCloseDrive(drivePath, [](Error e){
        conlock(cout) << "syncclosedrive result: "
            << errorstring(e)
            << endl;
    });
}

void exec_syncimport(autocomplete::ACState& s)
{
    if (client->loggedin() != FULLACCOUNT)
    {
        cerr << "You must be logged in to import syncs."
             << endl;
        return;
    }

    auto flags = std::ios::binary | std::ios::in;
    ifstream istream(s.words[2].s, flags);

    if (!istream)
    {
        cerr << "Unable to open "
             << s.words[2].s
             << " for reading."
             << endl;
        return;
    }

    string data;

    for (char buffer[512]; istream; )
    {
        istream.read(buffer, sizeof(buffer));
        data.append(buffer, static_cast<size_t>(istream.gcount()));
    }

    if (!istream.eof())
    {
        cerr << "Unable to read "
             << s.words[2].s
             << endl;
        return;
    }

    auto completion =
      [](error result)
      {
          if (result)
          {
              cerr << "Unable to import sync configs: "
                   << errorstring(result)
                   << endl;
              return;
          }

          cout << "Sync configs successfully imported."
               << endl;
      };

    cout << "Importing sync configs..."
         << endl;

    client->importSyncConfigs(data.c_str(), std::move(completion));
}

void exec_syncexport(autocomplete::ACState& s)
{
    if (client->loggedin() != FULLACCOUNT)
    {
        cerr << "You must be logged in to export syncs."
             << endl;
        return;
    }

    auto configs = client->syncs.exportSyncConfigs();

    if (s.words.size() == 2)
    {
        cout << "Configs exported as: "
             << configs
             << endl;
        return;
    }

    auto flags = std::ios::binary | std::ios::out | std::ios::trunc;
    ofstream ostream(s.words[2].s, flags);

    ostream.write(configs.data(), static_cast<std::streamsize>(configs.size()));
    ostream.close();

    if (!ostream.good())
    {
        cout << "Failed to write exported configs to: "
             << s.words[2].s
             << endl;
    }
}

void exec_syncopendrive(autocomplete::ACState& s)
{
    if (client->loggedin() != FULLACCOUNT)
    {
        cerr << "You must be logged in to restore backup syncs."
             << endl;
        return;
    }

    // sync backup restore drive
    const auto drivePath =
        localPathArg(s.words[2].s);

    client->syncs.backupOpenDrive(drivePath, [](Error e){
        conlock(cout) << "syncopendrive result: "
            << errorstring(e)
            << endl;
        });
}

void exec_synclist(autocomplete::ACState& s)
{
    // Check the user's logged in.
    if (client->loggedin() != FULLACCOUNT)
    {
        cerr << "You must be logged in to list syncs (and backup syncs)."
             << endl;
        return;
    }

    SyncConfigVector configs = client->syncs.getConfigs(false);

    if (configs.empty())
    {
        cout << "No syncs configured yet" << endl;
        return;
    }

    for (SyncConfig& config : configs)
    {

        // Display name.
        cout << "Sync "
            << config.mName
            << " Id: "
            << toHandle(config.mBackupId)
            << "\n";

        auto cloudnode = client->nodeByHandle(config.mRemoteNode);
        string cloudpath = cloudnode ? cloudnode->displaypath() : "<null>";

        // Display source/target mapping.
        cout << "  Mapping: "
            << config.mLocalPath.toPath(false)
            << " -> "
            << cloudpath
            << (!cloudnode || cloudpath != config.mOriginalPathOfRemoteRootNode ? " (originally " + config.mOriginalPathOfRemoteRootNode + ")" : "")
            << "\n";


        string runStateName;
        switch (config.mRunState)
        {
        case SyncRunState::Pending: runStateName = "PENDING"; break;
        case SyncRunState::Loading: runStateName = "LOADING"; break;
        case SyncRunState::Run: runStateName = "RUNNING"; break;
        case SyncRunState::Pause: runStateName = "PAUSED"; break;
        case SyncRunState::Suspend: runStateName = "SUSPENDED"; break;
        case SyncRunState::Disable: runStateName = "DISABLED"; break;
        }

        // Display status info.
        cout << "  State: " << runStateName << " "
            << "\n";

        //    // Display some usage stats.
        //    cout << "  Statistics: "
        //         //<< sync->localbytes
        //         //<< " byte(s) across "
        //         << sync->localnodes[FILENODE]
        //         << " file(s) and "
        //         << sync->localnodes[FOLDERNODE]
        //         << " folder(s).\n";
        //}
        //else

        // Display what status info we can.
        auto msg = config.syncErrorToStr();
        cout << "  Enabled: "
            << config.getEnabled()
            << "\n"
            << "  Last Error: "
            << msg
            << "\n";

        // Display sync type.
        cout << "  Type: "
            << (config.isExternal() ? "EX" : "IN")
            << "TERNAL "
            << SyncConfig::synctypename(config.getType())
            << "\n";

        // Display change detection method.
        cout << "  Change Detection Method: "
             << changeDetectionMethodToString(config.mChangeDetectionMethod)
             << "\n";

        if (CDM_PERIODIC_SCANNING == config.mChangeDetectionMethod)
        {
            // Display scan interval.
            cout << "  Scan Interval (seconds): "
                 << config.mScanIntervalSec
                 << "\n";
        }

        std::promise<bool> synchronous;
        client->syncs.collectSyncNameConflicts(config.mBackupId, [&synchronous](list<NameConflict>&& conflicts){
            for (auto& c : conflicts)
            {
                if (!c.cloudPath.empty() || !c.clashingCloud.empty())
                {
                    cout << "  Cloud Path conflict at " << c.cloudPath << ": ";
                    for (auto& n : c.clashingCloud)
                    {
                        cout << n.name << " ";
                    }
                    cout << "\n";
                }
                if (!c.localPath.empty() || !c.clashingLocalNames.empty())
                {
                    cout << "  Local Path conflict at " << c.localPath.toPath(false) << ": ";
                    for (auto& n : c.clashingLocalNames)
                    {
                        cout << n.toPath(false) << " ";
                    }
                    cout << "\n";
                }
            }
            cout << std::flush;
            synchronous.set_value(true);
        }, false);  // false so executes on sync thread - we are blocked here on client thread in single-threaded megacli.
        synchronous.get_future().get();
    }

    SyncStallInfo stall;
    if (client->syncs.syncStallDetected(stall))
    {
        auto cl = conlock(cout);
        cout << "Stalled (mutually unresolvable changes detected)!" << endl;
        for (auto& syncStallInfoMapPair : stall.syncStallInfoMaps)
        {
            cout << "=== [SyncID: " << syncStallInfoMapPair.first << "]" << endl;
            auto& syncStallInfoMap = syncStallInfoMapPair.second;
            cout << "noProgress: " << syncStallInfoMap.noProgress << ", noProgressCount: " << syncStallInfoMap.noProgressCount << " [HasProgressLack: " << std::string(syncStallInfoMap.hasProgressLack() ? "true" : "false") << "]" << endl;
            for (auto& p : syncStallInfoMap.cloud)
            {
                cout << "stall issue: " << syncWaitReasonDebugString(p.second.reason) << endl;
                string r1 = p.second.cloudPath1.debugReport();
                string r2 = p.second.cloudPath2.debugReport();
                string r3 = p.second.localPath1.debugReport();
                string r4 = p.second.localPath2.debugReport();
                if (!r1.empty()) cout << "    MEGA:" << r1 << endl;
                if (!r2.empty()) cout << "    MEGA:" << r2 << endl;
                if (!r3.empty()) cout << "    here:" << r3 << endl;
                if (!r4.empty()) cout << "    here:" << r4 << endl;
            }
            for (auto& p : syncStallInfoMap.local)
            {
                cout << "stall issue: " << syncWaitReasonDebugString(p.second.reason) << endl;
                string r1 = p.second.cloudPath1.debugReport();
                string r2 = p.second.cloudPath2.debugReport();
                string r3 = p.second.localPath1.debugReport();
                string r4 = p.second.localPath2.debugReport();
                if (!r1.empty()) cout << "    MEGA:" << r1 << endl;
                if (!r2.empty()) cout << "    MEGA:" << r2 << endl;
                if (!r3.empty()) cout << "    here:" << r3 << endl;
                if (!r4.empty()) cout << "    here:" << r4 << endl;
            }
        }
    }
}

void exec_syncremove(autocomplete::ACState& s)
{
    // Are we logged in?
    if (client->loggedin() != FULLACCOUNT)
    {
        cerr << "You must be logged in to manipulate syncs."
             << endl;
        return;
    }

    string localPath;
    string remotePath;
    bool byLocal = s.extractflagparam("-by-local-path", localPath);
    bool byRemote = s.extractflagparam("-by-remote-path", remotePath);

    // Get move destination
    size_t bkpDestPos = (byLocal || byRemote) ? 5 : 4;
    handle bkpDest = UNDEF;
    if (s.words.size() > bkpDestPos)
    {
        // get final destination
        std::shared_ptr<Node> destination = nodebypath(s.words[bkpDestPos].s.c_str());
        if (destination)
        {
            bkpDest = destination->nodehandle;
        }
        else
        {
            cout << "Wrong backup remove destination: " << s.words[bkpDestPos].s << endl;
            return;
        }
    }

    std::function<bool(SyncConfig&, Sync*)> predicate;
    bool found = false;
    bool isBackup = false;

    if (byLocal)
    {
        predicate = [&](SyncConfig& config, Sync*) {
            auto matched = config.mLocalPath.toPath(false) == localPath;

            found = found || matched;
            isBackup |= config.isBackup();

            return matched;
        };
    }
    else if (byRemote)
    {
        predicate = [&](SyncConfig& config, Sync*) {
            auto matched = config.mOriginalPathOfRemoteRootNode == remotePath;

            found = found || matched;
            isBackup |= config.isBackup();

            return matched;
        };
    }
    else
    {
        predicate = [&](SyncConfig& config, Sync*) {
            auto id = toHandle(config.mBackupId);
            auto matched = id == s.words[2].s;

            found = found || matched;
            isBackup |= config.isBackup();

            return matched;
        };
    }

    auto v = client->syncs.selectedSyncConfigs(predicate);

    if (v.size() != 1)
    {
        cerr << "Found " << v.size() << " matching syncs." << endl;
        return;
    }

    std::function<void(Error e)> completion = [=](Error e)
        {
            if (e == API_OK)
            {
                cout << "Sync - removed" << endl;
            }
            else if (e == API_ENOENT)
            {
                cout << "Sync - no config exists for "
                    << (byLocal ? localPath : (byRemote ? remotePath : s.words[2].s));
            }
            else
            {
                cout << "Sync - Failed to remove (" << error(e) << ": " << errorstring(e) << ')' << endl;
            }
        };

    if (v[0].isBackup())
    {
        // unlink the backup's Vault nodes after deregistering it
        NodeHandle source = v[0].mRemoteNode;
        NodeHandle destination = NodeHandle().set6byte(bkpDest);
        completion = [completion, source, destination](Error e){
            client->unlinkOrMoveBackupNodes(source, destination, completion);
        };
    }

    client->syncs.deregisterThenRemoveSyncById(v[0].mBackupId, std::move(completion));
}

void exec_syncstatus(autocomplete::ACState& s)
{
    // Are we logged in?
    if (client->loggedin() != FULLACCOUNT)
    {
        cerr << "You must be logged in to display the status of syncs."
             << endl;
        return;
    }

    // sync status [id]
    handle id = UNDEF;

    // Is the user interested in a particular sync?
    if (s.words.size() == 3)
        Base64::atob(s.words[2].s.c_str(),
                     reinterpret_cast<byte*>(&id),
                     sizeof(id));

    // Compute the aggregate transfer speed of the specified syncs.
    map<handle, size_t> speeds;

    for (auto* slot : client->tslots)
    {
        // No FA? Not in progress.
        if (!slot->fa)
            continue;

        // Determine the transfer's current speed.
        auto speed = slot->mTransferSpeed.getCircularMeanSpeed();

        // Find out which syncs, if any, are related to this transfer.
        for (auto* file : slot->transfer->files)
        {
            // Not a sync transfer? Not interested!
            if (!file->syncxfer)
                continue;

            // Get our hands on this sync's thread-safe state.
            auto state = static_cast<SyncTransfer_inClient*>(file)->syncThreadSafeState;

            // Is it a sync we're interested in?
            if (id != UNDEF && id != state->backupId())
                continue;

            // Make sure the speed's never negative.
            speed = std::max<m_off_t>(0, speed);

            // Add this transfer's speed to the sync's aggregate total.
            speeds[state->backupId()] += static_cast<size_t>(speed);
        }
    }

    // Convenience.
    using SV = vector<SyncStatusInfo>;

    std::promise<SV> waiter;

    // Retrieve status information from the engine.
    client->syncs.getSyncStatusInfo(id, [&](SV info) {
        waiter.set_value(std::move(info));
    }, false);

    // Wait for the engine to gather our information.
    auto results = waiter.get_future().get();

    // Was anything actually retrieved?
    if (results.empty())
    {
        // Was the user interested in a specific sync?
        if (id != UNDEF)
        {
            cerr << "Couldn't find an active sync with the ID: "
                 << toHandle(id)
                 << endl;
            return;
        }

        // User was interested in all active syncs.
        cerr << "There are no active syncs to report on."
             << endl;
        return;
    }

    // Translate size to a suffixed string.
    auto toSuffixedString = [](size_t value) {
        if (value < 1024)
            return std::to_string(value) + "B";

        const char* suffix = "?KMGTPE";

        while (value >= 1024)
            ++suffix, value /= 1024;

        return std::to_string(value) + *suffix + "B";
    };

    // Display status information to the user.
    for (auto& info : results)
    {
        cout << "Sync "
             << toHandle(info.mBackupID)
             << ":\n"
             << "  Name: "
             << info.mName
             << "\n"
             << "  Total number of synced nodes: "
             << info.mTotalSyncedNodes
             << "\n"
             << "  Total size of synced files: "
             << toSuffixedString(info.mTotalSyncedBytes)
             << "\n"
             << "  Transfer progress: "
             << info.mTransferCounts.progress(0) * 100.0
             << "%\n"
             << "  Transfer speed: "
             << toSuffixedString(speeds[info.mBackupID])
             << "/s\n";
    }
}

void exec_syncxable(autocomplete::ACState& s)
{
    // Are we logged in?
    if (client->loggedin() != FULLACCOUNT)
    {
        cerr << "You must be logged in to manipulate syncs."
             << endl;
        return;
    }

    string errIdString;
    bool withError = s.extractflagparam("-error", errIdString);

    auto targetState = SyncRunState::Run;

    if (s.words[1].s == "run") targetState = SyncRunState::Run;
    else if (s.words[1].s == "pause") targetState = SyncRunState::Pause;
    else if (s.words[1].s == "suspend") targetState = SyncRunState::Suspend;
    else if (s.words[1].s == "disable") targetState = SyncRunState::Disable;

    handle backupId = 0;
    Base64::atob(s.words[2].s.c_str(), (byte*) &backupId, sizeof(handle));

    SyncConfig config;
    if (!client->syncs.configById(backupId, config))
    {
        cout << "No sync found with id: " << Base64Str<sizeof(handle)>(backupId) << endl;
        return;
    }

    if (config.mRunState == targetState)
    {
        cout << "Sync is already in that state" << endl;
        return;
    }


    switch (targetState)
    {
    case SyncRunState::Pending:
    case SyncRunState::Loading:
    case SyncRunState::Run:
    {
        // sync enable id
        client->syncs.enableSyncByBackupId(backupId, true, [](error err, SyncError serr, handle)
            {
                if (err)
                {
                    cerr << "Unable to enable sync: "
                        << errorstring(err)
                        << endl;
                }
                else
                {
                    cout << "Sync Running." << endl;
                }
            }, true, "");

        break;
    }
    case SyncRunState::Pause:
    case SyncRunState::Suspend:
    case SyncRunState::Disable:
    {
        if (targetState == SyncRunState::Pause)
        {
            LOG_warn << "[exec_syncxable] Target state: SyncRunState::Pause. Sync will be suspended";
        }
        bool keepSyncDb = targetState == SyncRunState::Pause || targetState == SyncRunState::Suspend;

        client->syncs.disableSyncByBackupId(
            backupId,
            static_cast<SyncError>(withError ? atoi(errIdString.c_str()) : 0),
            false,
            keepSyncDb,
            [targetState](){
                cout << (targetState == SyncRunState::Suspend || targetState == SyncRunState::Pause ? "Sync Suspended." : "Sync Disabled.") << endl;
                });
        break;
    }
    }
}

#endif // ENABLE_SYNC

std::string setTypeToString(Set::SetType t)
{
    const std::string tStr = std::to_string(t);
    switch (t)
    {
    case Set::TYPE_ALBUM: return "Photo Album (" + tStr + ")";
    case Set::TYPE_PLAYLIST: return "Video Playlist (" + tStr + ")";
    default:               return "Unexpected Set Type with value " + tStr;
    }
}

void printSet(const Set* s)
{
    if (!s)
    {
        cout << "Set not found" << endl;
        return;
    }

    cout << "Set " << toHandle(s->id()) << endl;
    cout << "\ttype: " << setTypeToString(s->type()) << endl;
    cout << "\tpublic id: " << toHandle(s->publicId()) << endl;
    cout << "\tkey: " << Base64::btoa(s->key()) << endl;
    cout << "\tuser: " << toHandle(s->user()) << endl;
    cout << "\tts: " << s->ts() << endl;
    cout << "\tname: " << s->name() << endl;
    handle cover = s->cover();
    cout << "\tcover: " << (cover == UNDEF ? "(no cover)" : toHandle(cover)) << endl;
    cout << endl;
}
void printElements(const elementsmap_t* elems)
{
    if (!elems)
    {
        cout << "No elements" << endl;
        return;
    }

    for (const auto& p : *elems)
    {
        const SetElement& el = p.second;
        cout << "\t\telement " << toHandle(el.id()) << endl;
        cout << "\t\t\tset: " << toHandle(el.set()) << endl;
        cout << "\t\t\tname: " << el.name() << endl;
        cout << "\t\t\torder: " << el.order() << endl;
        cout << "\t\t\tkey: " << (el.key().empty() ? "(no key)" : Base64::btoa(el.key())) << endl;
        cout << "\t\t\tts: " << el.ts() << endl;
        cout << "\t\t\tnode: " << toNodeHandle(el.node()) << endl;
        if (el.nodeMetadata())
        {
            cout << "\t\t\t\tfile name: " << el.nodeMetadata()->filename << endl;
            cout << "\t\t\t\tfile size: " << el.nodeMetadata()->s << endl;
            cout << "\t\t\t\tfile attrs: " << el.nodeMetadata()->fa << endl;
            cout << "\t\t\t\tfingerprint: " << el.nodeMetadata()->fingerprint << endl;
            cout << "\t\t\t\tts: " << el.nodeMetadata()->ts << endl;
            cout << "\t\t\t\towner: " << toHandle(el.nodeMetadata()->u) << endl;
        }
    }
    cout << endl;
}

void exec_setsandelements(autocomplete::ACState& s)
{
    static const set<string> nonLoggedInCmds {"fetchpublicset",
                                              "getsetinpreview",
                                              "downloadelement",
                                              "stoppublicsetpreview"
                                             };

    const auto command = s.words[1].s;
    const auto commandRequiresLoggingIn = [&command]() -> bool
    {
        return nonLoggedInCmds.find(command) == nonLoggedInCmds.end(); // contains is C++20
    };
    const auto isClientLoggedIn = []() -> bool
    {
        return client->loggedin() == FULLACCOUNT;
    };

    // Are we logged in?
    if (commandRequiresLoggingIn() && !isClientLoggedIn())
    {
        cerr << "You must be logged in to manipulate Sets. "
             << "Except for the following commands:\n";
        for (const auto& c : nonLoggedInCmds) cerr << "\t" << c << "\n";
        return;
    }

    if (command == "list")
    {
        const auto& sets = client->getSets();
        for (auto& set : sets)
        {
            printSet(&set.second);
            printElements(client->getSetElements(set.first));
        }
    }

    else if (command == "newset")
    {
        if (s.words.size() < 3)
        {
            cout << "Wrong number of parameters. Try again\n";
            return;
        }

        const char* name = (s.words.size() == 4) ? s.words[3].s.c_str() : nullptr;
        Set newset;
        if (name)
        {
            newset.setName(name);
        }
        Set::SetType t = static_cast<Set::SetType>(stoi(s.words[2].s));
        newset.setType(t);

        client->putSet(std::move(newset), [](Error e, const Set* s)
            {
                if (e == API_OK && s)
                {
                    cout << "Created Set with id " << toHandle(s->id()) << endl;
                    printSet(s);
                }
                else
                {
                    cout << "Error creating new Set " << e << endl;
                }
            });
    }

    else if (command == "updateset")
    {
        handle id = 0; // must have remaining bits set to 0
        Base64::atob(s.words[2].s.c_str(), (byte*)&id, MegaClient::SETHANDLE);

        Set updset;
        updset.setId(id);
        string buf;
        if (s.extractflagparam("-n", buf) || s.extractflag("-n"))
        {
            updset.setName(std::move(buf));
        }
        buf.clear();
        if (s.extractflagparam("-c", buf) || s.extractflag("-c"))
        {
            if (buf.empty())
            {
                updset.setCover(UNDEF);
            }
            else
            {
                handle hc = 0;
                Base64::atob(buf.c_str(), (byte*)&hc, MegaClient::SETELEMENTHANDLE);
                updset.setCover(hc);
            }
        }

        client->putSet(std::move(updset), [id](Error e, const Set*)
            {
                if (e == API_OK)
                {
                    cout << "Updated Set " << toHandle(id) << endl;
                    printSet(client->getSet(id));
                    printElements(client->getSetElements(id));
                }
                else
                {
                    cout << "Error updating Set " << toHandle(id) << ' ' << e << endl;
                }
            });
    }

    else if (command == "removeset")
    {
        handle id = 0; // must have remaining bits set to 0
        Base64::atob(s.words[2].s.c_str(), (byte*)&id, MegaClient::SETHANDLE);

        client->removeSet(id, [id](Error e)
            {
                if (e == API_OK)
                    cout << "Removed Set " << toHandle(id) << endl;
                else
                    cout << "Error removing Set " << toHandle(id) << ' ' << e << endl;
            });
    }

    else if (command == "getsetinpreview")
    {
        if (!client->inPublicSetPreview())
        {
            cout << "Not in Public Set Preview currently\n";
            return;
        }
        const Set* ps = client->getPreviewSet();
        if (ps)
        {
            cout << "Fetched Set successfully\n";
            printSet(ps);
            printElements(client->getPreviewSetElements());
        }
        else cout << "Error getting Set from preview: No Set received\n";
    }

    else if (command == "removeelement")
    {
        handle sid = 0, eid = 0; // must have remaining bits set to 0
        Base64::atob(s.words[2].s.c_str(), (byte*)&sid, MegaClient::SETHANDLE);
        Base64::atob(s.words[3].s.c_str(), (byte*)&eid, MegaClient::SETELEMENTHANDLE);

        client->removeSetElement(sid, eid, [sid, eid](Error e)
            {
                if (e == API_OK)
                    cout << "Removed Element " << toHandle(eid) << " from Set " << toHandle(sid) << endl;
                else
                    cout << "Error removing Element " << toHandle(eid) << ' ' << e << endl;
            });
    }

    else if (command == "export")
    {
        handle sid = 0;
        Base64::atob(s.words[2].s.c_str(), (byte*)&sid, MegaClient::SETHANDLE);

        string buf;
        bool isExportSet = !(s.extractflagparam("-disable", buf) || s.extractflag("-disable"));
        buf.clear();
        cout << (isExportSet ? "En" : "Dis") << "abling export for Set " << toHandle(sid) << "\n";

        client->exportSet(sid, isExportSet, [sid, isExportSet](Error e)
           {
               string msg = (isExportSet ? "en" : "dis") + string("abled");
               cout << "\tSet " << toHandle(sid) << " export "
                    << (isExportSet ? "en" : "dis") << "abled "
                    << (e == API_OK ? "" : "un") << "successfully"
                    << (e == API_OK ? "" : ". " + verboseErrorString(e))
                    << endl;
           });
    }

    else if (command == "getpubliclink")
    {
        handle sid = 0; // must have remaining bits set to 0
        Base64::atob(s.words[2].s.c_str(), (byte*)&sid, MegaClient::SETHANDLE);
        cout << "Requesting public link for Set " << toHandle(sid) << endl;

        error e; string url;
        std::tie(e, url) = client->getPublicSetLink(sid);

        cout << "\tPublic link generated " << (e == API_OK ? "" : "un") << "successfully"
             << (e == API_OK ? " " + url : ". " + verboseErrorString(e))
             << endl;
    }

    else if (command == "fetchpublicset")
    {
        string publicSetLink = s.words[2].s.c_str();

        cout << "Fetching public Set with link " << publicSetLink << endl;
        client->fetchPublicSet(publicSetLink.c_str(), [](Error e, Set* s, elementsmap_t* elements)
        {
            unique_ptr<mega::Set> set(s);
            unique_ptr<mega::elementsmap_t> els(elements);
            if (e == API_OK)
            {
                if (set) cout << "\tPreview mode started for Set " << toHandle(set->id()) << endl;
                else cout << "\tNull Set returned for started preview mode\n";

                printSet(set.get());
                printElements(els.get());
            }
            else cout << "\tPreview mode failed: " + verboseErrorString(e) << endl;
        });
    }

    else if (command == "stoppublicsetpreview")
    {
        if (client->inPublicSetPreview())
        {
            cout << "Stopping Public Set preview mode for Set " << toHandle(client->getPreviewSet()->id()) << "\n";
            client->stopSetPreview();
            cout << "Public Set preview mode stopped " << (client->inPublicSetPreview() ? "un" : "")
                 << "successfully\n";
        }
        else cout << "Not in Public Set Preview mode currently\n";
    }

    else if (command == "downloadelement")
    {
        handle sid = 0, eid = 0;
        Base64::atob(s.words[2].s.c_str(), (byte*)&sid, MegaClient::SETHANDLE);
        Base64::atob(s.words[3].s.c_str(), (byte*)&eid, MegaClient::SETELEMENTHANDLE);
        cout << "Requesting to download Element " << toHandle(eid) << " from Set " << toHandle(sid)
             << endl;

        cout << "\tSet preview mode " << (client->inPublicSetPreview() ? "en" : "dis") << "abled\n";
        const SetElement* element = nullptr;
        m_off_t fileSize = 0;
        string fileName;
        string fingerprint;
        string fileattrstring;

        if (client->inPublicSetPreview())
        {
            element = client->getPreviewSetElement(eid);
            if (element)
            {
                cout << "\tElement found in preview Set\n";

                if (element->nodeMetadata()) // only present starting with 'aft' v2
                {
                    fileSize = element->nodeMetadata()->s;
                    fileName = element->nodeMetadata()->filename;
                    fingerprint = element->nodeMetadata()->fingerprint;
                    fileattrstring = element->nodeMetadata()->fa;
                }
            }
            else if (!isClientLoggedIn())
            {
                cout << "Error: attempting to dowload an element which is not in the previewed "
                     << "Set, and user is not logged in\n";
                return;
            }
        }
        if (!element)
        {
            element = client->getSetElement(sid, eid);
            if (element)
            {
                cout << "\tElement found in owned Set\n";

                std::shared_ptr<Node> mn(client->nodebyhandle(element->node()));

                if (!mn)
                {
                    cout << "\tElement node not found\n";
                    return;
                }
                fileSize = mn->size;
                fileName = mn->displayname();
                mn->serializefingerprint(&fingerprint);
                fileattrstring = mn->fileattrstring;
            }
        }

        if (!element)
        {
            cout << "\tElement not found as part of provided Set\n";
            return;
        }

        FileFingerprint ffp;
        m_time_t tm = 0;
        if (ffp.unserializefingerprint(&fingerprint)) tm = ffp.mtime;

        cout << "\tName: " << fileName << ", size: " << fileSize << ", tm: " << tm;
        if (!fingerprint.empty()) cout << ", fingerprint available";
        if (!fileattrstring.empty()) cout << ", has attributes";
        cout << endl;

        cout << "\tInitiating download..." << endl;

        TransferDbCommitter committer(client->tctable);
        auto file = std::make_unique<AppFileGet>(nullptr,
                                                    NodeHandle().set6byte(element->node()),
                                                    reinterpret_cast<const byte*>(element->key().c_str()), fileSize, tm,
                                                    &fileName, &fingerprint);
        file->hprivate = true;
        file->hforeign = true;
        startxfer(committer, std::move(file), fileName, client->nextreqtag());
    }

    else // create or update element
    {
        handle setId = 0;   // must have remaining bits set to 0
        handle node = 0;    // must have remaining bits set to 0
        handle elemId = 0;  // must have remaining bits set to 0
        Base64::atob(s.words[2].s.c_str(), (byte*)&setId, MegaClient::SETHANDLE);

        bool createNew = command == "newelement";
        if (createNew)
        {
            Base64::atob(s.words[3].s.c_str(), (byte*)&node, MegaClient::NODEHANDLE);
            elemId = UNDEF;
        }

        else // "updateelement"
        {
            node = UNDEF;
            Base64::atob(s.words[3].s.c_str(), (byte*)&elemId, MegaClient::SETELEMENTHANDLE);
        }

        SetElement el;
        el.setSet(setId);
        el.setId(elemId);
        el.setNode(node);

        string param;
        if (s.extractflagparam("-n", param) || s.extractflag("-n"))
        {
            el.setName(std::move(param));
        }
        param.clear();
        if (s.extractflagparam("-o", param))
        {
            el.setOrder(atoll(param.c_str()));
            if (el.order() == 0 && param != "0")
            {
                cout << "Invalid order: " << param << endl;
                return;
            }
        }

        client->putSetElement(std::move(el), [createNew, setId, elemId](Error e, const SetElement* el)
            {
                if (createNew)
                {
                    if (e == API_OK && el)
                        cout << "Created Element " << toHandle(el->id()) << " in Set " << toHandle(setId) << endl;
                    else
                        cout << "Error creating new Element " << e << endl;
                }
                else
                {
                    if (e == API_OK)
                    {
                        cout << "Updated Element " << toHandle(elemId) << " in Set " << toHandle(setId) << endl;
                    }
                    else
                    {
                        cout << "Error updating Element " << toHandle(elemId) << ' ' << e << endl;
                    }
                }
            });
    }
}

void exec_reqstat(autocomplete::ACState &s)
{
    bool turnon = s.extractflag("-on");
    bool turnoff = s.extractflag("-off");

    if (turnon)
    {
        client->startRequestStatusMonitor();
    }
    else if (turnoff)
    {
        client->stopRequestStatusMonitor();
    }

    cout << "Request status monitor: " << (client->requestStatusMonitorEnabled() ? "on" : "off") << endl;
}

void DemoApp::reqstat_progress(int permilprogress)
{
    cout << "Progress (per mille) of request: " << permilprogress << endl;
}

void exec_getABTestValue(autocomplete::ACState &s)
{
    string flag = s.words[1].s;

    unique_ptr<uint32_t> v = client->mABTestFlags.get(flag);
    string value = v ? std::to_string(*v) : "(not set)";

    cout << "[" << flag<< "]:" << value << endl;
}

void exec_sendABTestActive(autocomplete::ACState &s)
{
    string flag = s.words[1].s;

    client->sendABTestActive(flag.c_str(), [](Error e)
        {
            if (e)
            {
                cout << "Error sending Ab Test flag: " << e << endl;
            }
            else
            {
                cout << "Flag has been correctly sent." << endl;
            }
        });
}

void exec_contactVerificationWarning(autocomplete::ACState& s)
{
    bool enable = s.extractflag("-on");
    bool disable = s.extractflag("-off");

    if (enable)
    {
        client->setContactVerificationWarning(true,
          [](Error e)
          {
              if (!e) cout << "Warnings for unverified contacts: Enabled.";
          });
    }
    else if (disable)
    {
        client->setContactVerificationWarning(false,
          [](Error e)
          {
              if (!e) cout << "Warnings for unverified contacts: Disabled.";
          });
    }
    else
    {
        cout << "Warnings for unverified contacts: " << (client->mKeyManager.getContactVerificationWarning() ? "Enabled" : "Disabled") << endl;
    }
}

void exec_numberofnodes(autocomplete::ACState &s)
{
    uint64_t numberOfNodes = client->mNodeManager.getNodeCount();
    // We have to add RootNode, Incoming and rubbish
    if (!client->loggedIntoFolder())
    {
        numberOfNodes += 3;
    }
    cout << "Total nodes: " << numberOfNodes << endl;
    cout << "Total nodes in RAM: " << client->mNodeManager.getNumberNodesInRam() << endl << endl;

    cout << "Number of outShares: " << client->mNodeManager.getNodesWithOutShares().size();
}

void exec_numberofchildren(autocomplete::ACState &s)
{
    std::shared_ptr<Node> n;

    if (s.words.size() > 1)
    {
        n = nodebypath(s.words[1].s.c_str());
        if (!n)
        {
            cout << s.words[1].s << ": No such file or directory" << endl;
            return;
        }
    }
    else
    {
        n = client->nodeByHandle(cwd);
    }

    assert(n);

    size_t folders = client->mNodeManager.getNumberOfChildrenByType(n->nodeHandle(), FOLDERNODE);
    size_t files = client->mNodeManager.getNumberOfChildrenByType(n->nodeHandle(), FILENODE);

    cout << "Number of folders: " << folders << endl;
    cout << "Number of files: " << files << endl;
}

void exec_searchbyname(autocomplete::ACState &s)
{
    if (s.words.size() >= 2)
    {
        bool recursive = !s.extractflag("-norecursive");
        bool noSensitive = s.extractflag("-nosensitive");

        NodeHandle nodeHandle;
        if (s.words.size() == 3)
        {
            handle h;
            Base64::atob(s.words[2].s.c_str(), (byte*)&h, MegaClient::NODEHANDLE);
            nodeHandle.set6byte(h);
        }

        if (!recursive && nodeHandle.isUndef())
        {
            cout << "Search no recursive need node handle" << endl;
            return;
        }

        NodeSearchFilter filter;
        filter.byAncestors({nodeHandle.as8byte(), UNDEF, UNDEF});
        filter.byName(s.words[1].s);
        filter.bySensitivity(noSensitive ? NodeSearchFilter::BoolFilter::onlyTrue :
                                           NodeSearchFilter::BoolFilter::disabled);

        sharedNode_vector nodes;
        if (recursive)
        {
            nodes = client->mNodeManager.searchNodes(filter,
                                                     0 /*Order none*/,
                                                     CancelToken(),
                                                     NodeSearchPage{0, 0});
        }
        else
        {
            nodes = client->mNodeManager.getChildren(filter,
                                                     0 /*Order none*/,
                                                     CancelToken(),
                                                     NodeSearchPage{0, 0});
        }

        for (const auto& node : nodes)
        {
            cout << "Node: " << node->nodeHandle() << "    Name: " << node->displayname() << endl;
        }
    }
}

void exec_manualverif(autocomplete::ACState &s)
{
    if (s.extractflag("-on"))
    {
        client->mKeyManager.setManualVerificationFlag(true);
    }
    else if (s.extractflag("-off"))
    {
        client->mKeyManager.setManualVerificationFlag(false);
    }
}

/* MEGA VPN commands */
void exec_getvpnregions(autocomplete::ACState& s)
{
    cout << "Getting the list of VPN regions" << endl;
    client->getVpnRegions([]
            (const Error& e, std::vector<VpnRegion>&& vpnRegions)
            {
                if (e == API_OK)
                {
                    cout << "List of VPN regions:" << endl;
                    for (size_t i = 0; i < vpnRegions.size(); i++)
                    {
                        cout << (i+1) << ". " << vpnRegions[i].getName() << "." << endl;
                    }
                }
                else
                {
                    cout << "Getting the MEGA VPN credentials for the user failed. Error value: " << e << ". Reason: '" << errorstring(e) << "'" << endl;
                }
            });
}

void exec_getvpncredentials(autocomplete::ACState& s)
{
    cout << "Getting the MEGA VPN credentials for the user" << endl;
    string slotIDstr;
    int slotID{-1};
    if (s.extractflagparam("-s", slotIDstr))
    {
        try
        {
            slotID = std::stoi(slotIDstr);
        }
        catch (const std::exception& ex)
        {
            cout << "Could not convert param SlotID(" << slotIDstr << ") to integer. Exception: " << ex.what() << endl;
            return;
        }
    }
    bool showVpnRegions = !s.extractflag("-noregions");

    client->getVpnCredentials([slotID, showVpnRegions]
            (const Error& e,
            CommandGetVpnCredentials::MapSlotIDToCredentialInfo&& mapSlotIDToCredentialInfo, /* Map of SlotID: { ClusterID, IPv4, IPv6, DeviceID } */
            CommandGetVpnCredentials::MapClusterPublicKeys&& mapClusterPubKeys, /* Map of ClusterID: Cluster Public Key */
            std::vector<VpnRegion>&& vpnRegions /* VPN Regions */)
            {
                if (e == API_OK)
                {
                    cout << endl;
                    if (slotID > 0)
                    {
                        auto slotInfo = mapSlotIDToCredentialInfo.find(slotID);
                        if (slotInfo != mapSlotIDToCredentialInfo.end())
                        {
                            cout << "====================================================================" << endl;
                            cout << "SlotID: " << slotInfo->first << endl;
                            auto& credentialInfo = slotInfo->second;
                            cout << "ClusterID: " << credentialInfo.clusterID << endl;
                            cout << "Cluster Public Key: ";
                            auto clusterPublicKey = mapClusterPubKeys.find(credentialInfo.clusterID);
                            if (clusterPublicKey != mapClusterPubKeys.end())
                            {
                                cout << clusterPublicKey->second << endl;
                            }
                            else
                            {
                                cout << "Not found" << endl;
                            }
                            cout << "IPv4: " << credentialInfo.ipv4 << endl;
                            cout << "IPv6: " << credentialInfo.ipv6 << endl;
                            cout << "DeviceID: " << credentialInfo.deviceID << endl;
                            cout << "====================================================================" << endl;
                        }
                        else
                        {
                            cout << "There are no MEGA VPN credentials on SlotID " << slotID << endl;
                        }
                    }
                    else
                    {
                        if (mapSlotIDToCredentialInfo.empty())
                        {
                            cout << "List of VPN slots is EMPTY" << endl;
                        }
                        else
                        {
                            cout << "List of VPN slots:\n" << endl;
                            cout << "====================================================================" << endl;
                            for (auto& vpnSlot : mapSlotIDToCredentialInfo)
                            {
                                cout << "SlotID: " << vpnSlot.first << endl;
                                auto& credentialInfo = vpnSlot.second;
                                cout << "ClusterID: " << credentialInfo.clusterID << endl;
                                cout << "IPv4: " << credentialInfo.ipv4 << endl;
                                cout << "IPv6: " << credentialInfo.ipv6 << endl;
                                cout << "DeviceID: " << credentialInfo.deviceID << endl;
                                cout << "====================================================================" << endl;
                            }
                        }
                        cout << endl;

                        if (mapClusterPubKeys.empty())
                        {
                            cout << "List of Cluster Public Keys is EMPTY" << endl;
                        }
                        else
                        {
                            cout << "List of Cluster Public Keys:\n" << endl;
                            cout << "==========================================================================" << endl;
                            for (auto& clusterPubKey : mapClusterPubKeys)
                            {
                                cout << "ClusterID: " << clusterPubKey.first << ". ";
                                cout << "Public Key: " << clusterPubKey.second << endl;
                            }
                            cout << "==========================================================================" << endl;
                        }
                    }

                    if (showVpnRegions)
                    {
                        if (vpnRegions.empty())
                        {
                            cout << "List of VPN regions is EMPTY" << endl;
                        }
                        else
                        {
                            cout << "\nList of VPN regions:\n" << endl;
                            cout << "===================================================" << endl;
                            for (size_t i = 0; i < vpnRegions.size(); i++)
                            {
                                cout << (i+1) << ". " << vpnRegions[i].getName() << "." << endl;
                            }
                            cout << "===================================================" << endl;
                        }
                    }
                }
                else
                {
                    cout << "Getting the MEGA VPN credentials for the user failed. Error value: " << e << ". Reason: '";
                    switch(e)
                    {
                        case API_ENOENT:
                            cout << "The user has no credentials registered";
                            break;
                        default:
                            cout << errorstring(e);
                    }
                    cout << "'" << endl;
                }

            });
}

void exec_putvpncredential(autocomplete::ACState& s)
{
    string vpnRegion = s.words[1].s;
    cout << "Adding new MEGA VPN credentials. VPN region: " << vpnRegion << endl;
    string filename;
    if (s.extractflagparam("-file", filename))
    {
        filename.append(".conf");
        cout << "Credential data will be saved in: '" << filename << "'" << endl;
    }
    bool consoleoutput = !s.extractflag("-noconsole");
    client->putVpnCredential(std::move(vpnRegion),
            [filename, consoleoutput] (const Error& e, int slotID, std::string&& userPubKey, std::string&& newCredential)
            {
                if (e == API_OK && (slotID > 0) && !userPubKey.empty() && !newCredential.empty())
                {
                    cout << "\nNew MEGA VPN credential added successfully to slot " << slotID << endl;
                    cout << "User Public Key: " << userPubKey << endl;
                    if (consoleoutput || !filename.empty())
                    {
                        string credentialHeader;
                        credentialHeader.reserve(180);
                        credentialHeader.append("########################################\n")
                                        .append("#####     MEGA VPN credentials     #####\n")
                                        .append("#####     SlotID ").append(std::to_string(slotID)).append("                 #####\n")
                                        .append("########################################\n");
                        if (consoleoutput)
                        {
                            cout << "\n" << credentialHeader << newCredential << endl;
                        }
                        if (!filename.empty())
                        {
                            if (consoleoutput) { cout << endl; } // Leave a line between credential info and log info below
                            std::ofstream ostream(filename);

                            if (!ostream)
                            {
                                cerr << "Unable to open conf file for writing the new credential: '" << filename << "'" << endl;
                            }
                            else
                            {
                                ostream << credentialHeader << newCredential << endl;
                                if (ostream.flush())
                                {
                                    cout << "VPN credentials saved in: '" << filename << "'" << endl;
                                }
                                else
                                {
                                    cerr << "Encountered an error while writing conf file '" << filename << "'" << endl;
                                    return;
                                }
                            }
                        }
                    }
                }
                else
                {
                    cout << "Adding new MEGA VPN credentials failed. Error value: " << e << ". Reason: '";
                    switch(e)
                    {
                        case API_EARGS:
                            cout << "Peer Public Key does not have the correct format/length";
                            break;
                        case API_EACCESS:
                            cout << "Either the user is not a PRO user, the user is not logged in, or the peer Public Key is already taken";
                            break;
                        case API_ETOOMANY:
                            cout << "User has too many registered credentials";
                            break;
                        default:
                            cout << errorstring(e);
                    }
                    cout << "'" << endl;
                }
            });
}

void exec_delvpncredential(autocomplete::ACState& s)
{
    int slotID = stoi(s.words[1].s);
    cout << "Deleting the MEGA VPN credential on SlotID: " << slotID << endl;
    client->delVpnCredential(slotID,
            [slotID] (const Error& e)
            {
                cout << "MEGA VPN credential on slotID " << slotID << " ";
                if (e == API_OK)
                {
                    cout << "has been removed OK";
                }
                else
                {
                    cout << "has not been removed. Error value: " << e << ". Reason: '";
                    switch(e)
                    {
                        case API_EARGS:
                            cout << "SlotID is not valid";
                            break;
                        case API_ENOENT:
                            cout << "Slot was not occupied";
                            break;
                        default:
                            cout << errorstring(e);
                    }
                    cout << "'";
                }
                cout << endl;
            });
}

void exec_checkvpncredential(autocomplete::ACState& s)
{
    string userPubKey = s.words[1].s;
    cout << "Checking MEGA VPN credentials. User Public Key: " << userPubKey << endl;
    client->checkVpnCredential(userPubKey.c_str(), // To ensure a copy
            [userPubKey] (const Error& e)
            {
                cout << "MEGA VPN credentials with User Public Key: '" << userPubKey << "' ";
                if (e == API_OK)
                {
                    cout << "are valid";
                }
                else if (e == API_EACCESS)
                {
                    cout << "are not valid";
                }
                else
                {
                    cout << "could not be checked. Error value: " << e << ". Reason: '" << errorstring(e) << "'";
                }
                cout << endl;
            });
}
/* MEGA VPN commands */

void exec_fetchcreditcardinfo(autocomplete::ACState&)
{
    client->fetchCreditCardInfo([](const Error& e, const std::map<std::string, std::string>& creditCardInfo)
    {
        if (e == API_OK)
        {
            cout << "Credit card info: " << endl;
            for (const auto& it: creditCardInfo)
            {
                cout << "   " << it.first << ": " << it.second << endl;
            }
        }
        else
        {
            cout << "Error requesting credit card info: " << e << endl;
        }
    });
}

void exec_passwordmanager(autocomplete::ACState& s)
{
    static const set<string> nonLoggedInCmds {};

    const auto command = s.words[1].s;
    const auto commandRequiresLoggingIn = [&command]() -> bool
    {
        return nonLoggedInCmds.find(command) == nonLoggedInCmds.end();
    };
    const auto isClientLoggedIn = []() -> bool
    {
        return client->loggedin() == FULLACCOUNT;
    };

    // Are we logged in?
    if (commandRequiresLoggingIn() && !isClientLoggedIn())
    {
        cerr << "You must be logged in to manipulate Password items. "
             << (nonLoggedInCmds.empty() ? "" : "Except for the following commands:")
             << "\n";
        for (const auto& c : nonLoggedInCmds) cerr << "\t" << c << "\n";
        return;
    }

    const auto moreParamsThan = [&s](size_t min) -> bool
    {
        if (s.words.size() <= min)
        {
            cout << "Wrong parameters\n";
            return false;
        }
        return true;
    };
    const auto getNodeHandleFromParam = [&s](size_t paramPos) -> NodeHandle
    {
        handle nh;
        Base64::atob(s.words[paramPos].s.c_str(), (byte*)&nh, MegaClient::NODEHANDLE);
        return NodeHandle{}.set6byte(nh);
    };
    const auto createPwdData = [](std::string&& pwd,
                                  std::string&& url,
                                  std::string&& userName,
                                  std::string&& notes)
    {
        // patch to allow setting to null taking into account that extractflag doesn't accept ""
        const string EMPTY = "EMPTY";
        auto pwdData = std::make_unique<AttrMap>();
        if (!pwd.empty())
        {
            if (pwd == EMPTY) pwd.clear();
            pwdData->map[AttrMap::string2nameid(MegaClient::PWM_ATTR_PASSWORD_PWD)] = std::move(pwd);
        }
        if (!url.empty())
        {
            if (url == EMPTY) url.clear();
            pwdData->map[AttrMap::string2nameid(MegaClient::PWM_ATTR_PASSWORD_URL)] = std::move(url);
        }
        if (!userName.empty())
        {
            if (userName == EMPTY) userName.clear();
            pwdData->map[AttrMap::string2nameid(MegaClient::PWM_ATTR_PASSWORD_USERNAME)] = std::move(userName);
        }
        if (!notes.empty())
        {
            if (notes == EMPTY) notes.clear();
            pwdData->map[AttrMap::string2nameid(MegaClient::PWM_ATTR_PASSWORD_NOTES)] = std::move(notes);
        }
        return pwdData;
    };
    const auto printEntryDetails = [](NodeHandle nh)
    {
        auto pwdNode = client->nodeByHandle(nh);
        assert(pwdNode);
        assert(pwdNode->isPasswordNode());

        auto jsonPwdData = pwdNode->attrs.map[AttrMap::string2nameid(MegaClient::NODE_ATTR_PASSWORD_MANAGER)];
        AttrMap pwdData;
        pwdData.fromjson(jsonPwdData.c_str());
        cout << "Password data for entry " << pwdNode->attrs.map['n'] << " (" << toNodeHandle(nh) << "):\n";
        const auto printAttr = [&pwdData](const char* attr) -> void
        {
            const auto nid = AttrMap::string2nameid(attr);
            cout << "\t" << attr << ": " << (pwdData.map.contains(nid) ? pwdData.map[nid] : "") << "\n";
        };
        printAttr(MegaClient::PWM_ATTR_PASSWORD_PWD);
        printAttr(MegaClient::PWM_ATTR_PASSWORD_USERNAME);
        printAttr(MegaClient::PWM_ATTR_PASSWORD_URL);
        printAttr(MegaClient::PWM_ATTR_PASSWORD_NOTES);
    };

    if (command == "list")
    {
        auto n = client->nodeByHandle(client->getPasswordManagerBase());
        if (n)
        {
            dumptree(n.get(), true, 1, nullptr, nullptr);
        }
    }
    else if (command == "getbase")
    {
        cout << "Password Base handle is " << toNodeHandle(client->getPasswordManagerBase()) << "\n";
    }
    else if (command == "createbase")
    {
        const UserAttribute* attribute = client->ownuser()->getAttribute(ATTR_PWM_BASE);
        if (attribute && attribute->isValid())
        {
            assert(attribute->value().size() == MegaClient::NODEHANDLE);
            std::cout << "Password Manager Base already exists "
                      << toNodeHandle(&attribute->value()) << ". Skipping creation\n";
            return;
        }

        const auto cb = [](Error e, std::unique_ptr<NewNode> nn)
        {
            if (e == API_OK)
            {
                assert(nn);
                auto nh = nn->nodeHandle();
                // forced getUA because ATTR_PWM_BASE is not deletable / updatable
                client->getua(client->ownuser(), ATTR_PWM_BASE, -1, nullptr, [nh](byte*, unsigned, attr_t)
                {
                    std::cout << "Password Manager Base created with handle " << toNodeHandle(nh) << "\n";
                });
            }
            else
            {
                std::cout << "Error " << errorstring(e) << " during the creation of Password Manager Base\n";
            }
        };

        client->createPasswordManagerBase(-1, cb);
    }
    else if (command == "removebase")  // only doable in dev / debug conditions
    {
        #ifdef NDEBUG
        std::cout << "This command is only available in debug conditions for dev puporses\nn";
        #else
        const auto nhBase = client->getPasswordManagerBase();
        const auto mnBase = client->nodeByHandle(nhBase);

        client->senddevcommand("pwmhd", client->ownuser()->email.c_str());

        // forced erasing the user attribute and base folder node from Vault
        client->ownuser()->removeAttribute(ATTR_PWM_BASE);
        if (!mnBase) return;  // just in case there was a previous state where the node was deleted
        const bool keepVersions = false;
        const int tag = -1;
        const bool canChangeVault = true;
        auto cb = [nhBase](NodeHandle nh, Error e)
        {
            assert(nh == nhBase);
            const auto msg = "Password Manager Base " + toNodeHandle(nhBase);
            if (e == API_OK)
            {
                std::cout << msg << " and descendants erased\n";
            }
            else
            {
                std::cout << "Error " << errorstring(e) << " erasing " << msg << "\n";
            }

        };
        client->unlink(mnBase.get(), keepVersions, tag, canChangeVault, std::move(cb));
        #endif
    }
    else if (command == "newfolder")
    {
        if (!moreParamsThan(3)) return;

        auto ph = getNodeHandleFromParam(2);
        auto name = s.words[3].s.c_str();
        auto n = client->nodeByHandle(ph);
        if (!n)
        {
            cout << "Parent node with handle " << toNodeHandle(ph) << " not found\n";
            return;
        }

        client->createFolder(n, name, 0);
    }
    else if (command == "renamefolder" || command == "renameentry")
    {
        if (!moreParamsThan(3)) return;

        auto nh = getNodeHandleFromParam(2);
        auto newName = s.words[3].s.c_str();
        CommandSetAttr::Completion cb = [](NodeHandle nh, Error e)
        {
            if (e == API_OK) cout << "Node " << toNodeHandle(nh) << " renamed successfully\n";
            else cout << "Error renaming the node." << errorstring(e) << "\n";
        };

        client->renameNode(nh, newName, std::move(cb));
    }
    else if (command == "removefolder" || command == "removeentry")
    {
        if (!moreParamsThan(2)) return;

        auto nh = getNodeHandleFromParam(2);
        client->removeNode(nh, false, 0);
    }
    else if (command == "newentry")
    {
        if (!moreParamsThan(4)) return;

        auto ph = getNodeHandleFromParam(2);
        auto nParent = client->nodeByHandle(ph);
        if (!nParent)
        {
            cout << "Wrong parent handle provided " << toNodeHandle(ph) << "\n";
        }

        auto name = s.words[3].s.c_str();
        auto pwd = s.words[4].s.c_str();
        assert(*name && *pwd);

        string url; s.extractflagparam("-url", url);
        string userName; s.extractflagparam("-u", userName);
        string notes; s.extractflagparam("-n", notes);

        auto pwdData = createPwdData(std::string{pwd},
                                     std::move(url),
                                     std::move(userName),
                                     std::move(notes));

        client->createPasswordNode(name, std::move(pwdData), nParent, 0);
    }
    else if (command == "newentries")
    {
        if (s.words.size() <= 3)
        {
            cout << "Nothing to do\n";
            return;
        }
        auto ph = getNodeHandleFromParam(2);
        auto nParent = client->nodeByHandle(ph);
        if (!nParent)
        {
            cout << "Wrong parent handle provided " << toNodeHandle(ph) << "\n";
            return;
        }
        size_t currentReadIndex = 3;
        const size_t nWords = s.words.size();
        std::map<std::string, std::unique_ptr<AttrMap>> info;
        while (currentReadIndex < nWords)
        {
            auto name = s.words[currentReadIndex++].s.c_str();
            auto userName = s.words[currentReadIndex++].s.c_str();
            auto pwd = s.words[currentReadIndex++].s.c_str();
            assert(*name && *userName && *pwd);
            auto pwdData = createPwdData(std::string{pwd}, "", std::string{userName}, "");
            info[std::move(name)] = std::move(pwdData);
        }
        client->createPasswordNodes(std::move(info), nParent, 0);
    }
    else if (command == "getentrydata")
    {
        if (!moreParamsThan(2)) return;

        auto nh = getNodeHandleFromParam(2);
        auto pwdNode = client->nodeByHandle(nh);
        if (!pwdNode)
        {
            cout << "No node found with provided handle " << toNodeHandle(nh) << "\n";
            return;
        }
        if (!pwdNode->isPasswordNode())
        {
            cout << "Node handle provided " << toNodeHandle(nh) << " isn't a Password Node's\n";
            return;
        }

        printEntryDetails(nh);
    }
    else if (command == "updateentry")
    {
        if (!moreParamsThan(3)) return;

        auto nh = getNodeHandleFromParam(2);
        auto n = client->nodeByHandle(nh);
        if (!(n && n->isPasswordNode()))
        {
            cout << "Wrong Password node handle provided " << toNodeHandle(nh) << "\n";
        }

        string pwd; s.extractflagparam("-p", pwd);
        string url; s.extractflagparam("-url", url);
        string userName; s.extractflagparam("-u", userName);
        string notes; s.extractflagparam("-n", notes);

        auto pwdData = createPwdData(std::move(pwd),
                                     std::move(url),
                                     std::move(userName),
                                     std::move(notes));

        auto cb = [printEntryDetails](NodeHandle nh, Error e)
        {
            if (e == API_OK) printEntryDetails(nh);
            else std::cout << "Error: " << errorstring(e) << "\n";
        };

        client->updatePasswordNode(nh, std::move(pwdData), std::move(cb));
    }
    else
    {
        cout << command << " not recognized. Ignoring it\n";
    }

    if (!client->isClientType(MegaClient::ClientType::PASSWORD_MANAGER))
    {
        std::cout << "\n*****\n"
                  << "* Password Manager commands executed in a non-Password Manager MegaClient type.\n"
                  << "* Be wary of implications regarding fetch nodes and action packets received.\n"
                  << "* Check megacli help to start it as a Password Manager MegaClient type.\n"
                  << "*****\n\n";
    }
}

void exec_generatepassword(autocomplete::ACState& s)
{
    const auto command = s.words[1].s;

    if (command == "chars")
    {
        if (s.words.size() < 3)
        {
            cout << "Wrong parameters";
            return;
        }

        const auto length = static_cast<unsigned>(std::stoul(s.words[2].s));
        const bool useUpper = s.extractflag("-useUpper");
        const bool useDigits = s.extractflag("-useDigits");
        const bool useSymb = s.extractflag("-useSymbols");

        auto pwd = MegaClient::generatePasswordChars(useUpper, useDigits, useSymb, length);

        if (pwd.empty()) cout << "Error generating the password. Please check the logs (if active)\n";
        else cout << "Characers-based password successfully generated: " << pwd << "\n";
    }
}

void exec_importpasswordsfromgooglefile(autocomplete::ACState& s)
{
    auto localname = localPathArg(s.words[1].s);
    handle nh;
    Base64::atob(s.words[2].s.c_str(), (byte*)&nh, MegaClient::NODEHANDLE);
    NodeHandle parentHandle{};
    parentHandle.set6byte(nh);

    if (parentHandle.isUndef())
    {
        cout << "Parent handle is undef" << endl;
        return;
    }

    std::shared_ptr<Node> parent = client->mNodeManager.getNodeByHandle(parentHandle);
    if (!parent || !parent->isPasswordNodeFolder())
    {
        cout << "Invalid parent" << endl;
        return;
    }

    using namespace pwm::import;
    PassFileParseResult parserResult =
        readPasswordImportFile(localname.platformEncoded(), FileSource::GOOGLE_PASSWORD);
    if (parserResult.mErrCode != PassFileParseResult::ErrCode::OK)
    {
        cout << "Error importing file: " << parserResult.mErrMsg << endl;
        return;
    }

    sharedNode_list children = client->getChildren(parent.get());
    std::vector<std::string> childrenNames;
    std::transform(children.begin(),
                   children.end(),
                   std::back_inserter(childrenNames),
                   [](const std::shared_ptr<Node>& child) -> std::string
                   {
                       return child->displayname();
                   });
    ncoll::NameCollisionSolver solver{std::move(childrenNames)};

    const auto [badEntries, goodEntries] =
        MegaClient::validatePasswordEntries(std::move(parserResult.mResults), solver);

    std::cout << "Imported passwords: " << goodEntries.size()
              << "  Row with Error: " << badEntries.size() << endl;

    client->createPasswordNodes(std::move(goodEntries), parent, 0);

    if (!client->isClientType(MegaClient::ClientType::PASSWORD_MANAGER))
    {
        std::cout
            << "\n*****\n"
            << "* Password Manager commands executed in a non-Password Manager MegaClient type.\n"
            << "* Be wary of implications regarding fetch nodes and action packets received.\n"
            << "* Check megacli help to start it as a Password Manager MegaClient type.\n"
            << "*****\n\n";
    }
}

void exec_nodedescription(autocomplete::ACState& s)
{
    std::shared_ptr<Node> n = nodebypath(s.words[1].s.c_str());
    if (!n)
    {
        cout << s.words[1].s << ": No such file or directory" << endl;
        return;
    }

    const bool removeDescription = s.extractflag("-remove");
    const bool setDescription = s.extractflag("-set");
    const auto descNameId = AttrMap::string2nameid(MegaClient::NODE_ATTRIBUTE_DESCRIPTION);

    auto modifyDescription = [descNameId](const std::string& description, std::shared_ptr<Node> n)
    {
        AttrMap attrMap;
        attrMap.map[descNameId] = description;
        client->setattr(
            n,
            std::move(attrMap.map),
            [](NodeHandle h, Error e)
            {
                if (e == API_OK)
                    cout << "Description modified correctly" << endl;
                else
                    cout << "Error modifying description: " << e << "  Node: " << h << endl;
            },
            false);
    };

    if (removeDescription)
    {
        modifyDescription("", n);
    }
    else if (setDescription)
    {
        modifyDescription(s.words[2].s, n);
    }
    else if (auto it = n->attrs.map.find(descNameId); it != n->attrs.map.end())
    {
        cout << "Description: " << it->second << endl;
    }
    else
    {
        cout << "Description not set\n";
    }
}

void exec_nodesensitive(autocomplete::ACState& s)
{
    std::shared_ptr<Node> n = nodebypath(s.words[1].s.c_str());
    if (!n)
    {
        cout << s.words[1].s << ": No such file or directory" << endl;
        return;
    }

    const bool removeSensitive = s.extractflag("-remove");
    const auto attrId = AttrMap::string2nameid(MegaClient::NODE_ATTR_SEN);

    AttrMap attrMap;
    if (removeSensitive)
        attrMap.map[attrId] = "";
    else
        attrMap.map[attrId] = "1";

    client->setattr(
        n,
        std::move(attrMap.map),
        [removeSensitive](NodeHandle h, Error e)
        {
            if (e == API_OK)
                cout << "Node marked as " << (removeSensitive ? "no" : "") << " sensitive" << endl;
            else
                cout << "Error setting sensitivity: " << e << "  Node: " << h << endl;
        },
        false);
}

void exec_nodeTag(autocomplete::ACState& s)
{
    std::shared_ptr<Node> n = nodebypath(s.words[1].s.c_str());
    if (!n)
    {
        cout << s.words[1].s << ": No such file or directory\n";
        return;
    }

    const bool removeTag = s.extractflag("-remove");
    const bool addTag = s.extractflag("-add");
    const bool updateTag = s.extractflag("-update");
    const auto tagNameId = AttrMap::string2nameid(MegaClient::NODE_ATTRIBUTE_TAGS);

    if (removeTag)
    {
        client->removeTagFromNode(n,
                                  s.words[2].s,
                                  [](NodeHandle h, Error e)
                                  {
                                      if (e == API_OK)
                                          cout << "Tag removed correctly\n";
                                  });
    }
    else if (addTag)
    {
        client->addTagToNode(n,
                             s.words[2].s,
                             [](NodeHandle h, Error e)
                             {
                                 if (e == API_OK)
                                     cout << "Tag added correctly\n";
                             });
    }
    else if (updateTag)
    {
        client->updateTagNode(n,
                              s.words[2].s,
                              s.words[3].s,
                              [](NodeHandle h, Error e)
                              {
                                  if (e == API_OK)
                                      cout << "Tag updated correctly\n";
                              });
    }
    else if (auto it = n->attrs.map.find(tagNameId); it != n->attrs.map.end())
    {
        cout << "Tags: " << it->second << endl;
    }
    else
    {
        cout << "None tag is defined\n";
    }
}

void exec_getpricing(autocomplete::ACState& s)
{
    cout << "Getting pricing plans... " << endl;
    client->purchase_enumeratequotaitems();
}

void exec_collectAndPrintTransferStats(autocomplete::ACState& state)
{
    bool uploadsOnly = state.extractflag("-uploads");
    bool downloadsOnly = state.extractflag("-downloads");
    assert(!(uploadsOnly && downloadsOnly));

    auto collectAndPrintTransfersMetricsFromType = [](direction_t transferType)
    {
        std::cout << "\n===================================================================\n";
        std::cout << (transferType == PUT ? "[UploadStatistics]" : "[DownloadStatistics]") << "\n";
        std::cout << "Number of transfers: " << client->mTransferStatsManager.size(transferType)
                  << "\n";
        std::cout << "Max entries: " << client->mTransferStatsManager.getMaxEntries(transferType)
                  << "\n";
        std::cout << "Max age in seconds: "
                  << client->mTransferStatsManager.getMaxAgeSeconds(transferType) << "\n";
        std::cout << "-------------------------------------------------------------------\n";
        ::mega::stats::TransferStats::Metrics metrics =
            client->mTransferStatsManager.collectAndPrintMetrics(transferType);
        std::cout << metrics.toString() << "\n";
        std::cout << "-------------------------------------------------------------------\n";
        std::cout << "JSON format:\n";
        std::cout << metrics.toJson() << "\n";
        std::cout << "===================================================================\n\n";
    };

    if (!downloadsOnly)
    {
        collectAndPrintTransfersMetricsFromType(PUT);
    }

    if (!uploadsOnly)
    {
        collectAndPrintTransfersMetricsFromType(GET);
    }
}

void exec_hashcash(autocomplete::ACState& s)
{
    const static string originalUserAgent = client->useragent;
    const static string hashcashUserAgent = "HashcashDemo";

    if (s.words.size() == 1)
    {
        cout << "Hashcash demo is "
             << ((client->useragent == hashcashUserAgent) ? "enabled" : "disabled") << endl;
        return;
    }

    if (s.extractflag("-on"))
    {
        g_APIURL_default = "https://staging.api.mega.co.nz/";
        client->useragent = hashcashUserAgent;
    }
    else if (s.extractflag("-off"))
    {
        g_APIURL_default = "https://g.api.mega.co.nz/";
        client->useragent = originalUserAgent;
    }

    client->httpio->APIURL = g_APIURL_default;
    string tempUserAgent = client->useragent;
    client->httpio->setuseragent(&tempUserAgent);
    client->disconnect();
}
