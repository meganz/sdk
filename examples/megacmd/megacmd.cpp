/**
 * @file examples/megacmd.cpp
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

// requirements: linux & qt
#ifdef __linux__

#include "megacmd.h"
#include "mega.h"

#include "megacmdutils.h"
#include "configurationmanager.h"
#include "megacmdlogger.h"
#include "comunicationsmanager.h"
#include "synchronousrequestlistener.h"
#include "megaapi_impl.h" //to use such things as MegaThread. It might be interesting to move the typedefs to a separate .h file


#define USE_VARARGS
#define PREFER_STDARG

#include <readline/readline.h>
#include <readline/history.h>
#include <iomanip>
#include <string>

#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>

using namespace mega;

//void clear_display(){
//    rl_forced_update_display();
//}

//#define CLEAN_fatal if (SimpleLogger::logCurrentLevel < logFatal) ;\
//    else \
//        clear_display();
//#define CLEAN_err if (SimpleLogger::logCurrentLevel < logError) ;\
//    else \
//        clear_display();
//#define CLEAN_info if (SimpleLogger::logCurrentLevel < logInfo) ;\
//    else \
//        clear_display();
//#define CLEAN_debug if (SimpleLogger::logCurrentLevel < logDebug) ;\
//    else \
//        clear_display();
//#define CLEAN_warn if (SimpleLogger::logCurrentLevel < logWarning) ;\
//    else \
//        clear_display();
//#define CLEAN_verbose if (SimpleLogger::logCurrentLevel < logMax) ;\
//    else \
//        clear_display();

//MegaClient* client;
MegaApi *api;
//MegaApi *apiFolder;
std::queue<MegaApi *> apiFolders;
std::vector<MegaApi *> occupiedapiFolders;
MegaSemaphore semaphoreapiFolders;
MegaMutex mutexapiFolders;
//std::vector<MegaMutex *> mutexesApiFolder;
MegaCMDLogger *loggerCMD;

//Syncs
map<string,sync_struct *> syncsmap;
MegaMutex mtxSyncMap;

std::vector<MegaThread *> petitionThreads;

//Comunications Manager
ComunicationsManager * cm;

MegaFileSystemAccess *fsAccessCMD;

static AccountDetails account;

static handle cwd = UNDEF;
static char *session;

static const char* rootnodenames[] = { "ROOT", "INBOX", "RUBBISH" };

static const char* rootnodepaths[] = { "/", "//in", "//bin" };

static void store_line(char*);
static void process_line(char *);
static char* line;

static char dynamicprompt[128];

static const char* prompts[] =
{
    "MEGA CMD> ", "Password:", "Old Password:", "New Password:", "Retype New Password:"
};

enum prompttype
{
    COMMAND, LOGINPASSWORD, OLDPASSWORD, NEWPASSWORD, PASSWORDCONFIRM
};

static prompttype prompt = COMMAND;

static char pw_buf[256];
static int pw_buf_pos;

// local console
Console* console;

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
        OUTSTREAM << prompts[p] << flush;
        console->setecho(false);
    }
}


MegaApi* getFreeApiFolder(){
    semaphoreapiFolders.wait();
    mutexapiFolders.lock();
    MegaApi* toret=apiFolders.front();
    apiFolders.pop();
    occupiedapiFolders.push_back(toret);
    mutexapiFolders.unlock();
    return toret;
}
void freeApiFolder(MegaApi *apiFolder){
    mutexapiFolders.lock();
    occupiedapiFolders.erase(std::remove(occupiedapiFolders.begin(), occupiedapiFolders.end(), apiFolder), occupiedapiFolders.end());
    apiFolders.push(apiFolder);
    semaphoreapiFolders.release();
    mutexapiFolders.unlock();
}

class MegaCmdListener : public SynchronousRequestListener
{
private:
    float percentFetchnodes = 0.0f;
public:
    MegaCmdListener(MegaApi *megaApi, MegaRequestListener *listener = NULL);
    virtual ~MegaCmdListener();

    //Request callbacks
    virtual void onRequestStart(MegaApi* api, MegaRequest *request);
    virtual void doOnRequestFinish(MegaApi* api, MegaRequest *request, MegaError* e);
    virtual void onRequestUpdate(MegaApi* api, MegaRequest *request);
    virtual void onRequestTemporaryError(MegaApi *api, MegaRequest *request, MegaError* e);

protected:
    //virtual void customEvent(QEvent * event);

    MegaRequestListener *listener;
};


class MegaCmdTransferListener : public SynchronousTransferListener
{
private:
    float percentFetchnodes;
public:
    MegaCmdTransferListener(MegaApi *megaApi, MegaTransferListener *listener = NULL);
    virtual ~MegaCmdTransferListener();

    //Transfer callbacks
    virtual void onTransferStart(MegaApi* api, MegaTransfer *transfer);
    virtual void doOnTransferFinish(MegaApi* api, MegaTransfer *transfer, MegaError* e);
    virtual void onTransferUpdate(MegaApi* api, MegaTransfer *transfer);
    virtual void onTransferTemporaryError(MegaApi *api, MegaTransfer *transfer, MegaError* e);
    virtual bool onTransferData(MegaApi *api, MegaTransfer *transfer, char *buffer, size_t size);

protected:
    //virtual void customEvent(QEvent * event);

    MegaTransferListener *listener;
};

class MegaCmdGlobalListener : public MegaGlobalListener
{
public:
    void onNodesUpdate(MegaApi* api, MegaNodeList *nodes);
    void onUsersUpdate(MegaApi* api, MegaUserList *users);
    void onChatsUpdate(mega::MegaApi*, mega::MegaTextChatList*);
};

void MegaCmdGlobalListener::onChatsUpdate(mega::MegaApi*, mega::MegaTextChatList*){}

void MegaCmdGlobalListener::onUsersUpdate(MegaApi *api, MegaUserList *users)
{
    if (users)
    {
        if (users->size()==1)
        {
            LOG_info <<" 1 user received or updated" ;
        }
        else
        {
            LOG_info << users->size() << " users received or updated";
        }
    }
    else //initial update or too many changes
    {
        MegaUserList *users = api->getContacts();

        if (users)
        {
            if (users->size()==1)
            {
                LOG_info <<" 1 user received or updated" ;
            }
            else
            {
                LOG_info << users->size() << " users received or updated";
            }
            delete users;
        }
    }
}

void MegaCmdGlobalListener::onNodesUpdate(MegaApi *api, MegaNodeList *nodes)
{
    int nfolders = 0;
    int nfiles = 0;
    int rfolders = 0;
    int rfiles = 0;
    if (nodes)
    {
        for (int i=0;i<nodes->size();i++)
        {
            MegaNode *n = nodes->get(i);
            if (n->getType() == MegaNode::TYPE_FOLDER)
            {
                if (n->isRemoved()) rfolders++;
                else nfolders++;
            }
            else if (n->getType() == MegaNode::TYPE_FILE)
            {
                if (n->isRemoved()) rfiles++;
                else nfiles++;
            }
        }
    }
    else //initial update or too many changes
    {
        if (loggerCMD->getMaxLogLevel() >= logInfo)
        {
            MegaNode * nodeRoot= api->getRootNode();
            int * nFolderFiles = getNumFolderFiles(nodeRoot,api);
            nfolders+=nFolderFiles[0];
            nfiles+=nFolderFiles[1];
            delete []nFolderFiles;
            delete nodeRoot;

            MegaNode * inboxNode= api->getInboxNode();
            nFolderFiles = getNumFolderFiles(inboxNode,api);
            nfolders+=nFolderFiles[0];
            nfiles+=nFolderFiles[1];
            delete []nFolderFiles;
            delete inboxNode;

            MegaNode * rubbishNode= api->getRubbishNode();
            nFolderFiles = getNumFolderFiles(rubbishNode,api);
            nfolders+=nFolderFiles[0];
            nfiles+=nFolderFiles[1];
            delete []nFolderFiles;
            delete rubbishNode;

            MegaNodeList *inshares = api->getInShares();
            if (inshares)
            for (int i=0; i<inshares->size();i++)
            {
                nfolders++; //add the share itself
                nFolderFiles = getNumFolderFiles(inshares->get(i),api);
                nfolders+=nFolderFiles[0];
                nfiles+=nFolderFiles[1];
                delete []nFolderFiles;
            }
            delete inshares;
        }

        if (nfolders) { LOG_info << nfolders << " folders " << "added or updated "; }
        if (nfiles) { LOG_info << nfiles << " files " << "added or updated "; }
        if (rfolders) { LOG_info << rfolders << " folders " << "removed"; }
        if (rfiles) { LOG_info << rfiles << " files " << "removed"; }
    }
}

// global listener
MegaCmdGlobalListener* megaCmdGlobalListener;

// login e-mail address
static string login;

// new account signup e-mail address and name
static string signupemail, signupname;

//// signup code being confirmed
static string signupcode;

//// signup password challenge and encrypted master key
//static byte signuppwchallenge[SymmCipher::KEYLENGTH], signupencryptedmasterkey[SymmCipher::KEYLENGTH];

// loading progress of lengthy API responses
int responseprogress = -1;

const char * getUsageStr(const char *command)
{
    if(!strcmp(command,"login") ) return "login [email [password] | exportedfolderurl#key | session";
    if(!strcmp(command,"begin") ) return "begin [ephemeralhandle#ephemeralpw]";
    if(!strcmp(command,"signup") ) return "signup [email name|confirmationlink]";
    if(!strcmp(command,"confirm") ) return "confirm";
    if(!strcmp(command,"session") ) return "session";
    if(!strcmp(command,"mount") ) return "mount";
    if(!strcmp(command,"ls") ) return "ls [-lRr] [remotepath]";
    if(!strcmp(command,"cd") ) return "cd [remotepath]";
    if(!strcmp(command,"log") ) return "log [-sc] level";
    if(!strcmp(command,"pwd") ) return "pwd";
    if(!strcmp(command,"lcd") ) return "lcd [localpath]";
    if(!strcmp(command,"lpwd") ) return "lpwd";
    if(!strcmp(command,"import") ) return "import exportedfilelink#key";
//    if(!strcmp(command,"put") ) return "put localpattern [dstremotepath|dstemail:]";
    if(!strcmp(command,"put") ) return "put localfile [localfile2 localfile3 ...] [dstremotepath]";
    if(!strcmp(command,"putq") ) return "putq [cancelslot]";
    if(!strcmp(command,"get") ) return "get exportedlink#key|remotepath [localpath]";
    //if(!strcmp(command,"get") ) return "get remotepath [offset [length]]"; //TODO: implement this?
    //if(!strcmp(command,"get") ) return "get exportedfilelink#key [offset [length]]";
    if(!strcmp(command,"getq") ) return "getq [cancelslot]";
    if(!strcmp(command,"pause") ) return "pause [get|put] [hard] [status]";
    if(!strcmp(command,"getfa") ) return "getfa type [path] [cancel]";
    if(!strcmp(command,"mkdir") ) return "mkdir remotepath";
    if(!strcmp(command,"rm") ) return "rm remotepath";
    if(!strcmp(command,"mv") ) return "mv srcremotepath dstremotepath";
    if(!strcmp(command,"cp") ) return "cp srcremotepath dstremotepath|dstemail:";
    if(!strcmp(command,"sync") ) return "sync [localpath dstremotepath| [-ds] cancelslot]";
    if(!strcmp(command,"export") ) return "export remotepath [expireTime|del]";
    if(!strcmp(command,"share") ) return "share [remotepath [dstemail [r|rw|full] [origemail]]]";
    if(!strcmp(command,"invite") ) return "invite dstemail [origemail|del|rmd]";
    if(!strcmp(command,"ipc") ) return "ipc handle a|d|i";
    if(!strcmp(command,"showpcr") ) return "showpcr";
    if(!strcmp(command,"users") ) return "users";
    if(!strcmp(command,"getua") ) return "getua attrname [email]";
    if(!strcmp(command,"putua") ) return "putua attrname [del|set string|load file]";
    if(!strcmp(command,"putbps") ) return "putbps [limit|auto|none]";
    if(!strcmp(command,"killsession") ) return "killsession [all|sessionid]";
    if(!strcmp(command,"whoami") ) return "whoami";
    if(!strcmp(command,"passwd") ) return "passwd";
    if(!strcmp(command,"retry") ) return "retry";
    if(!strcmp(command,"recon") ) return "recon";
    if(!strcmp(command,"reload") ) return "reload";
    if(!strcmp(command,"logout") ) return "logout";
    if(!strcmp(command,"locallogout") ) return "locallogout";
    if(!strcmp(command,"symlink") ) return "symlink";
    if(!strcmp(command,"version") ) return "version";
    if(!strcmp(command,"debug") ) return "debug";
    if(!strcmp(command,"chatf") ) return "chatf ";
    if(!strcmp(command,"chatc") ) return "chatc group [email ro|rw|full|op]*";
    if(!strcmp(command,"chati") ) return "chati chatid email ro|rw|full|op";
    if(!strcmp(command,"chatr") ) return "chatr chatid [email]";
    if(!strcmp(command,"chatu") ) return "chatu chatid";
    if(!strcmp(command,"chatga") ) return "chatga chatid nodehandle uid";
    if(!strcmp(command,"chatra") ) return "chatra chatid nodehandle uid";
    if(!strcmp(command,"quit") ) return "quit";
    return "command not found";
}

bool validCommand(string thecommand){
    return getUsageStr(thecommand.c_str()) != "command not found";
}


string getHelpStr(const char *command)
{
    ostringstream os;

    os << getUsageStr(command) << endl;
    if(!strcmp(command,"login") )
    {
        os << "Logs in. Either with email and password, with session ID, or into an exportedfolder"
                                        << " If login into an exported folder indicate url#key" << endl;
    }
//    if(!strcmp(command,"begin") ) return "begin [ephemeralhandle#ephemeralpw]";
//    if(!strcmp(command,"signup") ) return "signup [email name|confirmationlink]";
//    if(!strcmp(command,"confirm") ) return "confirm";
    else if(!strcmp(command,"session") )
    {
        os << "Prints (secret) session ID" << endl;
    }
    else if(!strcmp(command,"mount") )
    {
        os << "Lists all the main nodes" << endl;
    }
    else if(!strcmp(command,"ls") )
    {
        os << "Lists files in a remote folder" << endl;
        os << "it accepts wildcards (? and *). e.g.: ls /a/b*/f00?.txt" << endl;
        os << endl;
        os << "Options:" << endl;
        os << " -R|-r" << "\t" << "list folders recursively" << endl;
        os << " -l" << "\t" << "include extra information" << endl;
    }
    else if(!strcmp(command,"cd") )
    {
        os << "Changes the current remote folder" << endl;
        os << endl;
        os << "If no folder is provided, it will be changed to the root folder" << endl;
    }
    else if(!strcmp(command,"log") )
    {
        os << "Prints/Modifies the current logs level" << endl;
        os << endl;
        os << "Options:" << endl;
        os << " -c" << "\t" << "CMD log level (higher). Messages captured by the command line." << endl;
        os << " -s" << "\t" << "SDK log level (lower). Messages captured by the engine and libs" << endl;
    }
    else if(!strcmp(command,"pwd") )
    {
        os << "Prints the current remote folder" << endl;
    }
    else if(!strcmp(command,"lcd") )
    {
        os << "Changes the current local folder for the interactive console" << endl;
        os << endl;
        os << "It will be used for uploads and downloads" << endl;
        os << endl;
        os << "If not using interactive console, the current local folder will be that of the shell executing mega comands" << endl;
    }
    else if(!strcmp(command,"lpwd") )
    {
        os << "Prints the current local folder for the interactive console" << endl;
        os << endl;
        os << "It will be used for uploads and downloads" << endl;
        os << endl;
        os << "If not using interactive console, the current local folder will be that of the shell executing mega comands" << endl;
    }
    else if(!strcmp(command,"logout") )
    {
        os << "Logs out, invalidating the session and the local caches" << endl;
    }
//    if(!strcmp(command,"import") ) return "import exportedfilelink#key";
    else if(!strcmp(command,"put") ) {
        os << "Uploads files/folders to a remote folder" << endl;
    }
//    if(!strcmp(command,"putq") ) return "putq [cancelslot]";
    else if(!strcmp(command,"get") )
    {
        os << "Downloads a remote file/folder or a public link " << endl;
        os << endl;
        os << "In case it is a file, the file will be downloaded at the specified folder (or at the current folder if none specified) " << endl;
        os << "If the file already exists, it will create a new one (e.g. \"file (1).txt\")" << endl;
        os << endl;
        os << "For folders, the entire contents (and the root folder itself) will be downloaded into the destination folder" << endl;
        os << "If the folder already exists, the contents will be merged with the downloaded one (preserving the existing files)" << endl;
    }
//    if(!strcmp(command,"getq") ) return "getq [cancelslot]";
//    if(!strcmp(command,"pause") ) return "pause [get|put] [hard] [status]";
//    if(!strcmp(command,"getfa") ) return "getfa type [path] [cancel]";
    else if(!strcmp(command,"mkdir") )
    {
        os << "Creates a directory or a directories hierarchy" << endl;
    }
    else if(!strcmp(command,"rm") )
    {
        os << "Recursively deletes a remote file/folder and all its descendents" << endl;
    }
    else if(!strcmp(command,"mv") )
    {
        os << "Moves a file/folder into a new location (all remotes)" << endl;
        os << endl;
        os << "If the location exists and is a folder, the source will be moved there" << endl;
        os << "If the location doesn't exits, the source will be renamed to the defined destiny" << endl;
    }
    else if(!strcmp(command,"cp") )
    {
        os << "Moves a file/folder into a new location (all remotes)" << endl;
        os << endl;
        os << "If the location exists and is a folder, the source will be copied there" << endl;
        os << "If the location doesn't exits, the file/folder will be renamed to the defined destiny" << endl;
    }
    else if(!strcmp(command,"sync") )
    {
        os << "Controls synchronizations" << endl;
        os << endl;
        os << "If no argument is provided, it lists current synchronization with their IDs and their state" << endl;
        os << endl;
        os << "If provided local and remote paths, it will start synchronizing a local folder into a remote folder" << endl;
        os << endl;
        os << "If an ID is provided, it will list such synchronization with its state, unless an option is specified:" << endl;
        os << "-d" << " " << "ID " << "\t" << "deletes a synchronization" << endl;
        os << "-s" << " " << "ID " << "\t" << "stops(pauses) a synchronization" << endl;
    }
//    if(!strcmp(command,"export") ) return "export remotepath [expireTime|del]";
//    if(!strcmp(command,"share") ) return "share [remotepath [dstemail [r|rw|full] [origemail]]]";
//    if(!strcmp(command,"invite") ) return "invite dstemail [origemail|del|rmd]";
//    if(!strcmp(command,"ipc") ) return "ipc handle a|d|i";
//    if(!strcmp(command,"showpcr") ) return "showpcr";
//    if(!strcmp(command,"users") ) return "users";
//    if(!strcmp(command,"getua") ) return "getua attrname [email]";
//    if(!strcmp(command,"putua") ) return "putua attrname [del|set string|load file]";
//    if(!strcmp(command,"putbps") ) return "putbps [limit|auto|none]";
//    if(!strcmp(command,"killsession") ) return "killsession [all|sessionid]";
    else if(!strcmp(command,"whoami") )
    {
        os << "Print info of the user" << endl;
        os << endl;
        os << "It will report info like total storage used, storage per main folder (see mount), pro level, account balance, and also the active sessions" << endl;
    }
//    if(!strcmp(command,"passwd") ) return "passwd";
//    if(!strcmp(command,"retry") ) return "retry";
//    if(!strcmp(command,"recon") ) return "recon";
    if(!strcmp(command,"reload") )
    {
        os << "Forces a reload of the remote files of the user" << endl;
    }
//    if(!strcmp(command,"logout") ) return "logout";
//    if(!strcmp(command,"locallogout") ) return "locallogout";
//    if(!strcmp(command,"symlink") ) return "symlink";
    if(!strcmp(command,"version") )
    {
        os << "Prints MEGA SDK version" << endl;
    }
//    if(!strcmp(command,"debug") ) return "debug";
//    if(!strcmp(command,"chatf") ) return "chatf ";
//    if(!strcmp(command,"chatc") ) return "chatc group [email ro|rw|full|op]*";
//    if(!strcmp(command,"chati") ) return "chati chatid email ro|rw|full|op";
//    if(!strcmp(command,"chatr") ) return "chatr chatid [email]";
//    if(!strcmp(command,"chatu") ) return "chatu chatid";
//    if(!strcmp(command,"chatga") ) return "chatga chatid nodehandle uid";
//    if(!strcmp(command,"chatra") ) return "chatra chatid nodehandle uid";
    if(!strcmp(command,"quit") )
    {
        os << "Quits" << endl;
        os << endl;
        os << "Notice that the session will still be active, and local caches available" << endl;
        os << "The session will be resumed when the service is restarted" << endl;
    }


    return os.str();
}


//AppFile::AppFile()
//{
//    static int nextseqno;

//    seqno = ++nextseqno;
//}

//// transfer start
//void AppFilePut::start()
//{
//}

//void AppFileGet::start()
//{
//}

//// transfer completion
//void AppFileGet::completed(Transfer*, LocalNode*)
//{
//    // (at this time, the file has already been placed in the final location)
//    delete this;
//}

//void AppFilePut::completed(Transfer* t, LocalNode*)
//{
//    // perform standard completion (place node in user filesystem etc.)
//    File::completed(t, NULL);

//    delete this;
//}

//AppFileGet::~AppFileGet()
//{
//    appxferq[GET].erase(appxfer_it);
//}

//AppFilePut::~AppFilePut()
//{
//    appxferq[PUT].erase(appxfer_it);
//}

//void AppFilePut::displayname(string* dname)
//{
//    *dname = localname;
//    transfer->client->fsaccess->local2name(dname);
//}

//// transfer progress callback
//void AppFile::progress()
//{
//}

//static void displaytransferdetails(Transfer* t, const char* action)
//{
//    string name;

//    for (file_list::iterator it = t->files.begin(); it != t->files.end(); it++)
//    {
//        if (it != t->files.begin())
//        {
//            OUTSTREAM << "/";
//        }

//        (*it)->displayname(&name);
//        OUTSTREAM << name.c_str();
//    }

//    OUTSTREAM << ": " << (t->type == GET ? "Incoming" : "Outgoing") << " file transfer " << action;
//}


/*
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
    OUTSTREAM << errorstring(e) << ")" << endl;
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
        OUTSTREAM << t->slot->progressreported * 10 / (1024 * (Waiter::ds - t->slot->starttime + 1)) << " KB/s" << endl;
    }
    else
    {
        OUTSTREAM << "delayed" << endl;
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
    OUTSTREAM << ", local data in this sync: " << sync->localbytes << " byte(s) in " << sync->localnodes[FILENODE]
         << " file(s) and " << sync->localnodes[FOLDERNODE] << " folder(s)" << endl;
}

void DemoApp::syncupdate_state(Sync*, syncstate_t newstate)
{
    switch (newstate)
    {
        case SYNC_ACTIVE:
            OUTSTREAM << "Sync is now active" << endl;
            break;

        case SYNC_FAILED:
            OUTSTREAM << "Sync failed." << endl;

        default:
            ;
    }
}

void DemoApp::syncupdate_scanning(bool active)
{
    if (active)
    {
        OUTSTREAM << "Sync - scanning files and folders" << endl;
    }
    else
    {
        OUTSTREAM << "Sync - scan completed" << endl;
    }
}

// sync update callbacks are for informational purposes only and must not change or delete the sync itself
void DemoApp::syncupdate_local_folder_addition(Sync* sync, LocalNode *, const char* path)
{
    OUTSTREAM << "Sync - local folder addition detected: " << path;
    syncstat(sync);
}

void DemoApp::syncupdate_local_folder_deletion(Sync* sync, LocalNode *localNode)
{
    OUTSTREAM << "Sync - local folder deletion detected: " << localNode->name;
    syncstat(sync);
}

void DemoApp::syncupdate_local_file_addition(Sync* sync, LocalNode *, const char* path)
{
    OUTSTREAM << "Sync - local file addition detected: " << path;
    syncstat(sync);
}

void DemoApp::syncupdate_local_file_deletion(Sync* sync, LocalNode *localNode)
{
    OUTSTREAM << "Sync - local file deletion detected: " << localNode->name;
    syncstat(sync);
}

void DemoApp::syncupdate_local_file_change(Sync* sync, LocalNode *, const char* path)
{
    OUTSTREAM << "Sync - local file change detected: " << path;
    syncstat(sync);
}

void DemoApp::syncupdate_local_move(Sync*, LocalNode *localNode, const char* path)
{
    OUTSTREAM << "Sync - local rename/move " << localNode->name << " -> " << path << endl;
}

void DemoApp::syncupdate_local_lockretry(bool locked)
{
    if (locked)
    {
        OUTSTREAM << "Sync - waiting for local filesystem lock" << endl;
    }
    else
    {
        OUTSTREAM << "Sync - local filesystem lock issue resolved, continuing..." << endl;
    }
}

void DemoApp::syncupdate_remote_move(Sync *, Node *n, Node *prevparent)
{
    OUTSTREAM << "Sync - remote move " << n->displayname() << ": " << (prevparent ? prevparent->displayname() : "?") <<
            " -> " << (n->parent ? n->parent->displayname() : "?") << endl;
}

void DemoApp::syncupdate_remote_rename(Sync *, Node *n, const char *prevname)
{
    OUTSTREAM << "Sync - remote rename " << prevname << " -> " <<  n->displayname() << endl;
}

void DemoApp::syncupdate_remote_folder_addition(Sync *, Node* n)
{
    OUTSTREAM << "Sync - remote folder addition detected " << n->displayname() << endl;
}

void DemoApp::syncupdate_remote_file_addition(Sync *, Node* n)
{
    OUTSTREAM << "Sync - remote file addition detected " << n->displayname() << endl;
}

void DemoApp::syncupdate_remote_folder_deletion(Sync *, Node* n)
{
    OUTSTREAM << "Sync - remote folder deletion detected " << n->displayname() << endl;
}

void DemoApp::syncupdate_remote_file_deletion(Sync *, Node* n)
{
    OUTSTREAM << "Sync - remote file deletion detected " << n->displayname() << endl;
}

void DemoApp::syncupdate_get(Sync*, Node *, const char* path)
{
    OUTSTREAM << "Sync - requesting file " << path << endl;
}

void DemoApp::syncupdate_put(Sync*, LocalNode *, const char* path)
{
    OUTSTREAM << "Sync - sending file " << path << endl;
}

void DemoApp::syncupdate_remote_copy(Sync*, const char* name)
{
    OUTSTREAM << "Sync - creating remote file " << name << " by copying existing remote file" << endl;
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
    OUTSTREAM << "Sync - state change of node " << l->name << " to " << treestatename(l->ts) << endl;
}

// generic name filter
// FIXME: configurable regexps
static bool is_syncable(const char* name)
{
    return *name != '.' && *name != '~' && strcmp(name, "Thumbs.db") && strcmp(name, "desktop.ini");
}

// determines whether remote node should be synced
bool DemoApp::sync_syncable(Node* n)
{
    return is_syncable(n->displayname());
}

// determines whether local file should be synced
bool DemoApp::sync_syncable(const char* name, string* localpath, string* localname)
{
    return is_syncable(name);
}
#endif

//AppFileGet::AppFileGet(Node* n, handle ch, byte* cfilekey, m_off_t csize, m_time_t cmtime, string* cfilename,
//                       string* cfingerprint)
//{
//    if (n)
//    {
//        h = n->nodehandle;
//        hprivate = true;

//        *(FileFingerprint*) this = *n;
//        name = n->displayname();
//    }
//    else
//    {
//        h = ch;
//        memcpy(filekey, cfilekey, sizeof filekey);
//        hprivate = false;

//        size = csize;
//        mtime = cmtime;

//        if (!cfingerprint->size() || !unserializefingerprint(cfingerprint))
//        {
//            memcpy(crc, filekey, sizeof crc);
//        }

//        name = *cfilename;
//    }

//    localname = name;
//    client->fsaccess->name2local(&localname);
//}

//AppFilePut::AppFilePut(string* clocalname, handle ch, const char* ctargetuser)
//{
//    // this assumes that the local OS uses an ASCII path separator, which should be true for most
//    string separator = client->fsaccess->localseparator;

//    // full local path
//    localname = *clocalname;

//    // target parent node
//    h = ch;

//    // target user
//    targetuser = ctargetuser;

//    // erase path component
//    name = *clocalname;
//    client->fsaccess->local2name(&name);
//    client->fsaccess->local2name(&separator);

//    name.erase(0, name.find_last_of(*separator.c_str()) + 1);
//}

// user addition/update (users never get deleted)
void DemoApp::users_updated(User** u, int count)
{
    if (count == 1)
    {
        OUTSTREAM << "1 user received or updated" << endl;
    }
    else
    {
        OUTSTREAM << count << " users received or updated" << endl;
    }
}

#ifdef ENABLE_CHAT

void DemoApp::chatcreate_result(TextChat *chat, error e)
{
    if (e)
    {
        OUTSTREAM << "Chat creation failed (" << errorstring(e) << ")" << endl;
    }
    else
    {
        OUTSTREAM << "Chat created successfully" << endl;
        printChatInformation(chat);
        OUTSTREAM << endl;
    }
}

void DemoApp::chatfetch_result(textchat_vector *chats, error e)
{
    if (e)
    {
        OUTSTREAM << "Chat fetching failed (" << errorstring(e) << ")" << endl;
    }
    else
    {
        if (chats->size() == 1)
        {
            OUTSTREAM << "1 chat received or updated" << endl;
        }
        else
        {
            OUTSTREAM << chats->size() << " chats received or updated" << endl;
        }

        for (textchat_vector::iterator it = chats->begin(); it < chats->end(); it++)
        {
            printChatInformation(*it);
            OUTSTREAM << endl;
        }
    }
}

void DemoApp::chatinvite_result(error e)
{
    if (e)
    {
        OUTSTREAM << "Chat invitation failed (" << errorstring(e) << ")" << endl;
    }
    else
    {
        OUTSTREAM << "Chat invitation successful" << endl;
    }
}

void DemoApp::chatremove_result(error e)
{
    if (e)
    {
        OUTSTREAM << "Peer removal failed (" << errorstring(e) << ")" << endl;
    }
    else
    {
        OUTSTREAM << "Peer removal successful" << endl;
    }
}

void DemoApp::chaturl_result(string *url, error e)
{
    if (e)
    {
        OUTSTREAM << "Chat URL retrieval failed (" << errorstring(e) << ")" << endl;
    }
    else
    {
        OUTSTREAM << "Chat URL: " << *url << endl;
    }

}

void DemoApp::chatgrantaccess_result(error e)
{
    if (e)
    {
        OUTSTREAM << "Grant access to node failed (" << errorstring(e) << ")" << endl;
    }
    else
    {
        OUTSTREAM << "Access to node granted successfully" << endl;
    }
}

void DemoApp::chatremoveaccess_result(error e)
{
    if (e)
    {
        OUTSTREAM << "Revoke access to node failed (" << errorstring(e) << ")" << endl;
    }
    else
    {
        OUTSTREAM << "Access to node removed successfully" << endl;
    }
}

void DemoApp::chats_updated(textchat_vector *chats)
{
    if (chats)
    {
        if (chats->size() == 1)
        {
            OUTSTREAM << "1 chat updated or created" << endl;
        }
        else
        {
            OUTSTREAM << chats->size() << " chats updated or created" << endl;
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

    OUTSTREAM << "Chat ID: " << hstr << endl;
    OUTSTREAM << "\tOwn privilege level: " << getPrivilegeString(chat->priv) << endl;
    OUTSTREAM << "\tChat shard: " << chat->shard << endl;
    OUTSTREAM << "\tURL: " << chat->url << endl;
    if (chat->group)
    {
        OUTSTREAM << "\tGroup chat: yes" << endl;
    }
    else
    {
        OUTSTREAM << "\tGroup chat: no" << endl;
    }
    OUTSTREAM << "\tPeers:";

    if (chat->userpriv)
    {
        OUTSTREAM << "\t\t(userhandle)\t(privilege level)" << endl;
        for (unsigned i = 0; i < chat->userpriv->size(); i++)
        {
            Base64::btoa((const byte *)&chat->userpriv->at(i).first, sizeof(handle), hstr);
            OUTSTREAM << "\t\t\t" << hstr;
            OUTSTREAM << "\t" << getPrivilegeString(chat->userpriv->at(i).second) << endl;
        }
    }
    else
    {
        OUTSTREAM << " no peers (only you as participant)" << endl;
    }
}

string DemoApp::getPrivilegeString(privilege_t priv)
{
    switch (priv)
    {
    case PRIV_FULL:
        return "PRIV_FULL (full access)";
    case PRIV_OPERATOR:
        return "PRIV_OPERATOR (operator)";
    case PRIV_RO:
        return "PRIV_RO (read-only)";
    case PRIV_RW:
        return "PRIV_RW (read-write)";
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
        OUTSTREAM << deletecount << " pending contact request" << (deletecount != 1 ? "s" : "") << " deleted" << endl;
    }
    if (updatecount != 0)
    {
        OUTSTREAM << updatecount << " pending contact request" << (updatecount != 1 ? "s" : "") << " received or updated" << endl;
    }
}

void DemoApp::setattr_result(handle, error e)
{
    if (e)
    {
        OUTSTREAM << "Node attribute update failed (" << errorstring(e) << ")" << endl;
    }
}

void DemoApp::rename_result(handle, error e)
{
    if (e)
    {
        OUTSTREAM << "Node move failed (" << errorstring(e) << ")" << endl;
    }
}

void DemoApp::unlink_result(handle, error e)
{
    if (e)
    {
        OUTSTREAM << "Node deletion failed (" << errorstring(e) << ")" << endl;
    }
}

void DemoApp::fetchnodes_result(error e)
{
    if (e)
    {
        OUTSTREAM << "File/folder retrieval failed (" << errorstring(e) << ")" << endl;
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
                OUTSTREAM << "File/folder retrieval succeed, but encryption key is wrong." << endl;
            }
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
            OUTSTREAM << "Success." << endl;
        }
    }

    if (e)
    {
        OUTSTREAM << "Node addition failed (" << errorstring(e) << ")" << endl;
    }
}

void DemoApp::share_result(error e)
{
    if (e)
    {
        OUTSTREAM << "Share creation/modification request failed (" << errorstring(e) << ")" << endl;
    }
}

void DemoApp::share_result(int, error e)
{
    if (e)
    {
        OUTSTREAM << "Share creation/modification failed (" << errorstring(e) << ")" << endl;
    }
    else
    {
        OUTSTREAM << "Share creation/modification succeeded" << endl;
    }
}

void DemoApp::setpcr_result(handle h, error e, opcactions_t action)
{
    if (e)
    {
        OUTSTREAM << "Outgoing pending contact request failed (" << errorstring(e) << ")" << endl;
    }
    else
    {
        if (h == UNDEF)
        {
            // must have been deleted
            OUTSTREAM << "Outgoing pending contact request " << (action == OPCA_DELETE ? "deleted" : "reminded") << " successfully" << endl;
        }
        else
        {
            char buffer[12];
            Base64::btoa((byte*)&h, sizeof(h), buffer);
            OUTSTREAM << "Outgoing pending contact request succeeded, id: " << buffer << endl;
        }
    }
}

void DemoApp::updatepcr_result(error e, ipcactions_t action)
{
    if (e)
    {
        OUTSTREAM << "Incoming pending contact request update failed (" << errorstring(e) << ")" << endl;
    }
    else
    {
        string labels[3] = {"accepted", "denied", "ignored"};
        OUTSTREAM << "Incoming pending contact request successfully " << labels[(int)action] << endl;
    }
}

void DemoApp::fa_complete(Node* n, fatype type, const char* data, uint32_t len)
{
    OUTSTREAM << "Got attribute of type " << type << " (" << len << " byte(s)) for " << n->displayname() << endl;
}

int DemoApp::fa_failed(handle, fatype type, int retries, error e)
{
    OUTSTREAM << "File attribute retrieval of type " << type << " failed (retries: " << retries << ") error: " << e << endl;

    return retries > 2;
}

void DemoApp::putfa_result(handle, fatype, error e)
{
    if (e)
    {
        OUTSTREAM << "File attribute attachment failed (" << errorstring(e) << ")" << endl;
    }
}

void DemoApp::invite_result(error e)
{
    if (e)
    {
        OUTSTREAM << "Invitation failed (" << errorstring(e) << ")" << endl;
    }
    else
    {
        OUTSTREAM << "Success." << endl;
    }
}

void DemoApp::putua_result(error e)
{
    if (e)
    {
        OUTSTREAM << "User attribute update failed (" << errorstring(e) << ")" << endl;
    }
    else
    {
        OUTSTREAM << "Success." << endl;
    }
}

void DemoApp::getua_result(error e)
{
    OUTSTREAM << "User attribute retrieval failed (" << errorstring(e) << ")" << endl;
}

void DemoApp::getua_result(byte* data, unsigned l)
{
    OUTSTREAM << "Received " << l << " byte(s) of user attribute: ";
    fwrite(data, 1, l, stdout);
    OUTSTREAM << endl;
}

void DemoApp::notify_retry(dstime dsdelta)
{
    if (dsdelta)
    {
        OUTSTREAM << "API request failed, retrying in " << dsdelta * 100 << " ms - Use 'retry' to retry immediately..."
             << endl;
    }
    else
    {
        OUTSTREAM << "Retried API request completed" << endl;
    }
}
*/

//static void nodestats(int* c, const char* action)
//{
//    if (c[FILENODE])
//    {
//        OUTSTREAM << c[FILENODE] << ((c[FILENODE] == 1) ? " file" : " files");
//    }
//    if (c[FILENODE] && c[FOLDERNODE])
//    {
//        OUTSTREAM << " and ";
//    }
//    if (c[FOLDERNODE])
//    {
//        OUTSTREAM << c[FOLDERNODE] << ((c[FOLDERNODE] == 1) ? " folder" : " folders");
//    }

//    if (c[FILENODE] || c[FOLDERNODE])
//    {
//        OUTSTREAM << " " << action << endl;
//    }
//}

// list available top-level nodes and contacts/incoming shares
static void listtrees()
{
//    //TODO: modify using API
    for (int i = 0; i < (int) (sizeof rootnodenames/sizeof *rootnodenames); i++)
    {
        OUTSTREAM << rootnodenames[i] << " on " << rootnodepaths[i] << endl;
    }

    MegaShareList * msl = api->getInSharesList();
    for (int i=0;i<msl->size();i++)
    {
        MegaShare *share = msl->get(i);
        MegaNode *n= api->getNodeByHandle(share->getNodeHandle());

        OUTSTREAM << "INSHARE on " << share->getUser() << ":" << n->getName() << " (" << getAccessLevelStr(share->getAccess()) << ")" << endl;
        delete n;
    }

    delete (msl);
}

string getCurrentLocalPath(){ //TODO: move all this into PosixFileSystemAccess
    char cCurrentPath[FILENAME_MAX];
    if (!getcwd(cCurrentPath, sizeof(cCurrentPath)))
    {
        LOG_err << "Couldn't read cwd";
        return "";
    }

    return string(cCurrentPath);
}

/**
 * @brief expanseLocalPath
 * Returns the full path
 * @param path
 * @return
 */
string expanseLocalPath(string path){ //TODO: posix dependent!
    ostringstream os;
    if (path.at(0)=='/')
    {
        return path;
    }
    else
    {
        os << getCurrentLocalPath() << "/" << path;
        return os.str();
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
// * : and / filename components, as well as the \, must be escaped by \.
// (correct UTF-8 encoding is assumed)
// returns NULL if path malformed or not found
static MegaNode* nodebypath(const char* ptr, string* user = NULL, string* namepart = NULL)
{
    vector<string> c;
    string s;
    int l = 0;
    const char* bptr = ptr;
    int remote = 0;
    MegaNode* n;
    MegaNode* nn;

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
        if (c.size() == 2 && !c[1].size())
        {
            if (user)
            {
                *user = c[0];
            }

            return NULL;
        }

        //TODO: implement finding users share node.
//        User* u;
//        itll be sth like: if ((u = finduser(c[0].c_str()))) //TODO: implement findUser
//        if ((u = client->finduser(c[0].c_str())))
//        {/*
//            // locate matching share from this user
//            handle_set::iterator sit;
//            string name;
//            for (sit = u->sharing.begin(); sit != u->sharing.end(); sit++)
//            {
//                if ((n = client->nodebyhandle(*sit)))
//                {
//                    if(!name.size())
//                    {
//                        name =  c[1];
//                        n->client->fsaccess->normalize(&name);
//                    }

//                    if (!strcmp(name.c_str(), n->displayname()))
//                    {
//                        l = 2;
//                        break;
//                    }
//                }
//            }
//        }*/

        if (!l)
        {
            return NULL;
        }
    }
    else //local
    {
        // path starting with /
        if (c.size() > 1 && !c[0].size())
        {
            // path starting with //
            if (c.size() > 2 && !c[1].size())
            {
                if (c[2] == "in")
                {
                    n = api->getInboxNode();
                }
                else if (c[2] == "bin")
                {
                    n = api->getRubbishNode();
                }
                else
                {
                    return NULL;
                }

                l = 3;
            }
            else
            {
                //TODO: modify using API //TODO: test & delete comments
                n = api->getRootNode();
//                n = client->nodebyhandle(client->rootnodes[0]);

                l = 1;
            }
        }
        else
        {
            n = api->getNodeByHandle(cwd);
        }
    }

    // parse relative path
    while (n && l < (int)c.size())
    {
        if (c[l] != ".")
        {
            if (c[l] == "..")
            {
                MegaNode * aux;
                aux = n;
                n = api->getParentNode(n);
                if (n!=aux) delete aux;
            }
            else
            {
                // locate child node (explicit ambiguity resolution: not implemented)
                if (c[l].size())
                {
                    nn = api->getChildNode(n, c[l].c_str());

                    if (!nn) //NOT FOUND
                    {
                        // mv command target? return name part of not found
                        if (namepart && l == (int) c.size() - 1) //if this is the last part, we will pass that one, so that a mv command know the name to give the the new node
                        {
                            *namepart = c[l];
                            return n;
                        }

                        delete n;
                        return NULL;
                    }

                    if (n!=nn) delete n;
                    n = nn;
                }
            }
        }

        l++;
    }

    return n;
}


bool patternMatches(const char *what,const char *pattern)
{
    //return std::regex_match (pattern, std::regex(what) ); //c++11

    // If we reach at the end of both strings, we are done
    if (*pattern == '\0' && *what == '\0')
        return true;

    // Make sure that the characters after '*' are present
    // in what string. This function assumes that the pattern
    // string will not contain two consecutive '*'
    if (*pattern == '*' && *(pattern+1) != '\0' && *what == '\0')
        return false;

    // If the pattern string contains '?', or current characters
    // of both strings match
    if (*pattern == '?' || *pattern == *what)
    {
        if(*what == '\0') return false;
        return patternMatches(what+1, pattern+1);
    }

    // If there is *, then there are two possibilities
    // a) We consider current character of what string
    // b) We ignore current character of what string.
    if (*pattern == '*')
        return patternMatches(what, pattern+1) || patternMatches(what+1, pattern);

    return false;
}


/**
  TODO: doc. delete of added MegaNodes * into nodesMatching responsibility for the caller
 * @brief getNodesMatching
 * @param parentNode
 * @param c
 * @param nodesMatching
 */
void getNodesMatching(MegaNode *parentNode, queue<string> pathParts, vector<MegaNode *> *nodesMatching)
{
    if (!pathParts.size()) return;

    string currentPart = pathParts.front();
    pathParts.pop();

    if (currentPart == ".")
    {
        getNodesMatching(parentNode, pathParts, nodesMatching);
    }


    MegaNodeList* children= api->getChildren(parentNode);
    if (children)
    {
        for (int i=0;i<children->size();i++)
        {
            MegaNode *childNode = children->get(i);
            if (patternMatches(childNode->getName(),currentPart.c_str()))
            {
                if (pathParts.size()==0) //last leave
                {
                    nodesMatching->push_back(childNode->copy());
                }
                else
                {
                    getNodesMatching(childNode, pathParts, nodesMatching);
                }
            }
        }
        delete children;
    }
}

MegaNode * getRootNodeByPath(const char *ptr, string* user = NULL)
{
    queue<string> c;
    string s;
    int l = 0;
    const char* bptr = ptr;
    int remote = 0;
    MegaNode* n;

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
                        c.push(s);
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

                    c.push(s);

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
        if (c.size() == 2 && !c.back().size())
        {
            if (user)
            {
                *user = c.front();
            }

            return NULL;
        }

        //TODO: implement finding users share node.
//        User* u;
//        itll be sth like: if ((u = finduser(c[0].c_str()))) //TODO: implement findUser
//        if ((u = client->finduser(c[0].c_str())))
//        {/*
//            // locate matching share from this user
//            handle_set::iterator sit;
//            string name;
//            for (sit = u->sharing.begin(); sit != u->sharing.end(); sit++)
//            {
//                if ((n = client->nodebyhandle(*sit)))
//                {
//                    if(!name.size())
//                    {
//                        name =  c[1];
//                        n->client->fsaccess->normalize(&name);
//                    }

//                    if (!strcmp(name.c_str(), n->displayname()))
//                    {
//                        l = 2;
//                        break;
//                    }
//                }
//            }
//        }*/

        if (!l)
        {
            return NULL;
        }
    }
    else //local
    {
        // path starting with /
        if (c.size() > 1 && !c.front().size())
        {
            c.pop();
            // path starting with //
            if (c.size() > 1 && !c.front().size())
            {
                c.pop();
                if (c.front() == "in")
                {
                    n = api->getInboxNode();
                    c.pop();
                }
                else if (c.front() == "bin")
                {
                    n = api->getRubbishNode();
                    c.pop();
                }
                else
                {
                    return NULL;
                }
            }
            else
            {
                n = api->getRootNode();
            }
        }
        else
        {
            n = api->getNodeByHandle(cwd);
        }
    }

    return n;
}


// returns node pointer determined by path relative to cwd
// path naming conventions:
// * path is relative to cwd
// * /path is relative to ROOT
// * //in is in INBOX
// * //bin is in RUBBISH
// * X: is user X's INBOX
// * X:SHARE is share SHARE from user X
// * : and / filename components, as well as the \, must be escaped by \.
// (correct UTF-8 encoding is assumed)
// returns NULL if path malformed or not found
// TODO: dosctrings, delete responsibility of the caller (included the meganodes within the list!!)
vector <MegaNode*> * nodesbypath(const char* ptr, string* user = NULL, string* namepart = NULL)
{
    vector<MegaNode *> *nodesMatching = new vector<MegaNode *> ();
    queue<string> c;
    string s;
    int l = 0;
    const char* bptr = ptr;
    int remote = 0;
    MegaNode* n;
    MegaNode* nn;

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
                        c.push(s);
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
                            return nodesMatching;
                        }

                        remote = 1;
                    }

                    if (ptr > bptr)
                    {
                        s.append(bptr, ptr - bptr);
                    }

                    bptr = ptr + 1;

                    c.push(s);

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
        if (c.size() == 2 && !c.back().size())
        {
            if (user)
            {
                *user = c.front();
            }

            return NULL;
        }

        //TODO: implement finding users share node.
//        User* u;
//        itll be sth like: if ((u = finduser(c[0].c_str()))) //TODO: implement findUser
//        if ((u = client->finduser(c[0].c_str())))
//        {/*
//            // locate matching share from this user
//            handle_set::iterator sit;
//            string name;
//            for (sit = u->sharing.begin(); sit != u->sharing.end(); sit++)
//            {
//                if ((n = client->nodebyhandle(*sit)))
//                {
//                    if(!name.size())
//                    {
//                        name =  c[1];
//                        n->client->fsaccess->normalize(&name);
//                    }

//                    if (!strcmp(name.c_str(), n->displayname()))
//                    {
//                        l = 2;
//                        break;
//                    }
//                }
//            }
//        }*/

        if (!l)
        {
            return NULL;
        }
    }
    else //local
    {
        // path starting with /
        if (c.size() > 1 && !c.front().size())
        {
            c.pop();
            // path starting with //
            if (c.size() > 1 && !c.front().size())
            {
                c.pop();
                if (c.front() == "in")
                {
                    n = api->getInboxNode();
                    c.pop();
                }
                else if (c.front() == "bin")
                {
                    n = api->getRubbishNode();
                    c.pop();
                }
                else
                {
                    return nodesMatching;
                }
            }
            else
            {
                n = api->getRootNode();
            }
        }
        else
        {
            n = api->getNodeByHandle(cwd);
        }
    }

    getNodesMatching(n, c, nodesMatching);

    return nodesMatching;
}




static void listnodeshares(MegaNode* n)
{
    MegaShareList* outShares=api->getOutShares(n);
    if(outShares)
    {
        for (int i=0;i<outShares->size();i++)
//        for (share_map::iterator it = n->outshares->begin(); it != n->outshares->end(); it++)
        {
            OUTSTREAM << "\t" << n->getName();

            if (outShares->get(i))
            {
                OUTSTREAM << ", shared with " << outShares->get(i)->getUser() << " (" << getAccessLevelStr(outShares->get(i)->getAccess()) << ")"
                     << endl;
            }
            else
            {
                OUTSTREAM << ", shared as exported folder link" << endl;
            }
        }
        delete outShares;
    }
}

//void TreeProcListOutShares::proc(MegaClient*, Node* n)
//{
//    listnodeshares(n);
//}

void dumpNode(MegaNode* n, int extended_info, int depth = 0, const char* title = NULL)
{

    if (!title && !(title = n->getName()))
    {
        title = "CRYPTO_ERROR";
    }

    if (depth)
    for (int i = depth-1; i--; )
    {
        OUTSTREAM << "\t";
    }

    OUTSTREAM << title;
    if (extended_info)
    {
        OUTSTREAM << " (";
        switch (n->getType())
        {
        case MegaNode::TYPE_FILE:
            OUTSTREAM << n->getSize();

            const char* p;
            if ((p = strchr(n->getAttrString()->c_str(), ':')))
            {
                OUTSTREAM << ", has attributes " << p + 1;
            }

            if (UNDEF != n->getPublicHandle())
                //if (n->plink)
            {
                OUTSTREAM << ", shared as exported";
                if (n->getExpirationTime()) //TODO: validate equivalence
                    //if (n->plink->ets)
                {
                    OUTSTREAM << " temporal";
                }
                else
                {
                    OUTSTREAM << " permanent";
                }
                OUTSTREAM << " file link";
            }
            break;

        case MegaNode::TYPE_FOLDER:
        {
            OUTSTREAM << "folder";
            MegaShareList* outShares = api->getOutShares(n);
            if (outShares)
            {
                for (int i=0;i<outShares->size();i++)
                {
                    if (outShares->get(i))
                    {
                        OUTSTREAM << ", shared with " << outShares->get(i)->getUser() << ", access "
                                  << getAccessLevelStr(outShares->get(i)->getAccess());
                    }
                }
                if (UNDEF != n->getPublicHandle())
                    //if (n->plink)
                {
                    OUTSTREAM << ", shared as exported";
                    if (n->getExpirationTime()) //TODO: validate equivalence
                        //                        if (n->plink->ets)
                    {
                        OUTSTREAM << " temporal";
                    }
                    else
                    {
                        OUTSTREAM << " permanent";
                    }
                    OUTSTREAM << " folder link";
                }
                delete outShares;
            }

            MegaShareList* pendingoutShares= api->getPendingOutShares(n);
            if(pendingoutShares)
            {
                for (int i=0;i<pendingoutShares->size();i++)
                {
                    if (pendingoutShares->get(i))
                    {
                        OUTSTREAM << ", shared (still pending) with " << pendingoutShares->get(i)->getUser() << ", access "
                                  << getAccessLevelStr(pendingoutShares->get(i)->getAccess());
                    }
                }
                delete pendingoutShares;
            }

            if (n->isInShare())
            {
                //OUTSTREAM << ", inbound " << getAccessLevelStr(n->inshare->access) << " share";
                OUTSTREAM << ", inbound " << api->getAccess(n) << " share"; //TODO: validate & delete
            }
            break;
        }

        default:
            OUTSTREAM << "unsupported type, please upgrade";
        }
        OUTSTREAM << ")" << (n->isRemoved() ? " (DELETED)" : "");
    }

    OUTSTREAM << endl;
}

static void dumptree(MegaNode* n, int recurse, int extended_info, int depth = 0, string pathRelativeTo = "NULL")
{
    if (depth)
    {
        if (pathRelativeTo != "NULL")
        {
            if (!n->getName()) dumpNode(n, extended_info, depth,"CRYPTO_ERROR");
            else
            {
                char * nodepath = api->getNodePath(n);


                char *pathToShow = NULL;
                if (pathRelativeTo != "")
                    pathToShow = strstr(nodepath,pathRelativeTo.c_str());

//                if (pathToShow == NULL )// || !strcmp(pathRelativeTo,"/"))
//                    pathToShow=nodepath;
//                else
//                {
                    if (pathToShow == nodepath) //found at beginning
                    {
                        pathToShow+=pathRelativeTo.size();
                        if (*pathToShow=='/' && pathRelativeTo != "/") pathToShow++;
                    }
                    else
                        pathToShow=nodepath;

//                }

                dumpNode(n, extended_info,depth,pathToShow);

                delete nodepath;
            }
        }
        else
            dumpNode(n, extended_info,depth);

        if (!recurse)
        {
            return;
        }
    }

    if (n->getType() != MegaNode::TYPE_FILE)
    {
        MegaNodeList* children= api->getChildren(n);
        if (children)
        {
            for (int i=0;i<children->size();i++)
            {
                dumptree(children->get(i), recurse, extended_info, depth + 1);
            }
        delete children;
        }
    }
}

static void nodepath(handle h, string* path)
{
    path->clear();

    MegaNode *rootNode = api->getRootNode();
    if ( rootNode  && (h == rootNode->getHandle()) )
    {
        *path = "/";
        delete rootNode;
        return;
    }
    delete rootNode;

    MegaNode* n = api->getNodeByHandle(h);

    while (n)
    {
        switch (n->getType())
        {
            case MegaNode::TYPE_FOLDER:
                path->insert(0, n->getName());

                if (n->isInShare())
                {
                    path->insert(0, ":");

                    if (const char * suser=getUserInSharedNode(n,api))
                    {
                        path->insert(0, suser);
                    }
                    else
                    {
                        path->insert(0, "UNKNOWN");
                    }
                    delete n;
                    return;
                }
                break;

            case MegaNode::TYPE_INCOMING:
                path->insert(0, "//in");
                delete n;
                return;

            case MegaNode::TYPE_ROOT:
                delete n;
                return;

            case MegaNode::TYPE_RUBBISH:
                path->insert(0, "//bin");
                delete n;
                return;

            case MegaNode::TYPE_UNKNOWN:
            case MegaNode::TYPE_FILE:
                path->insert(0, n->getName());
        }

        path->insert(0, "/");
        MegaNode *aux=n;
        n = api->getNodeByHandle(n->getParentHandle());
        delete aux;
    }
}

//appfile_list appxferq[2];

#ifdef __linux__
void sigint_handler(int signum)
{
    LOG_verbose << "Received signal: " << signum;
    rl_replace_line("", 0); //clean contents of actual command
    rl_crlf(); //move to nextline

    // reset position and print prompt
    pw_buf_pos = 0;
    OUTSTREAM << prompts[prompt] << flush;
}
#endif


////////////////////////////////////////
///      MegaCmdListener methods     ///
////////////////////////////////////////

void MegaCmdListener::onRequestStart(MegaApi* api, MegaRequest *request){
    if (!request)
    {
        LOG_err << " onRequestStart for undefined request ";
        return;
    }

    LOG_verbose << "onRequestStart request->getType(): " << request->getType();

//    switch(request->getType())
//    {
//        default:
//            LOG_debug << "onRequestStart of unregistered type of request: " << request->getType();
//            break;
//    }

    //clear_display();
}

void MegaCmdListener::doOnRequestFinish(MegaApi* api, MegaRequest *request, MegaError* e)
{
    if (!request)
    {
        LOG_err << " onRequestFinish for undefined request ";
        return;
    }

    LOG_verbose << "onRequestFinish request->getType(): " << request->getType();

    switch(request->getType())
    {
        case MegaRequest::TYPE_FETCH_NODES:
        {
            map<string,sync_struct *>::iterator itr;
            int i =0;
            for(itr = ConfigurationManager::configuredSyncs.begin(); itr != ConfigurationManager::configuredSyncs.end(); ++itr,i++)
            {
                sync_struct *thesync = ((sync_struct *)(*itr).second);

                MegaCmdListener *megaCmdListener = new MegaCmdListener(api,NULL);
                MegaNode * node = api->getNodeByHandle(thesync->handle);

                api->resumeSync(thesync->localpath.c_str(), node, thesync->fingerprint,megaCmdListener);
                megaCmdListener->wait();
                if (megaCmdListener->getError() && megaCmdListener->getError()->getErrorCode() == MegaError::API_OK)
                {
                    thesync->fingerprint = megaCmdListener->getRequest()->getNumber();
                    thesync->active = true;

                    syncsmap[thesync->localpath]=thesync;
                    char *nodepath = api->getNodePath(node);
                    LOG_info << "Loaded sync: " << thesync->localpath << " to " << nodepath;
                    delete []nodepath;
                }

                delete megaCmdListener;
                delete node;
            }
            break;
        }
        default:
//            LOG_debug << "onRequestFinish of unregistered type of request: " << request->getType();
// //            rl_message("");
// //            clear_display();
            break;
    }
    //clear_display();
}

void MegaCmdListener::onRequestUpdate(MegaApi* api, MegaRequest *request){
    if (!request)
    {
        LOG_err << " onRequestUpdate for undefined request ";
        return;
    }

    LOG_verbose << "onRequestUpdate request->getType(): " << request->getType();

    switch(request->getType())
    {
    case MegaRequest::TYPE_FETCH_NODES:
    {
#if defined(RL_ISSTATE) && defined(RL_STATE_INITIALIZED)
        int rows = 1,cols = 80;

        if (RL_ISSTATE(RL_STATE_INITIALIZED))
        {
            rl_resize_terminal();
            rl_get_screen_size (&rows,&cols);
        }
        char outputString[cols+1];
        for (int i=0;i<cols;i++) outputString[i]='.';
        outputString[cols]='\0';
        char * ptr = outputString;
        sprintf(ptr,"%s","Fetching nodes ||");
        ptr+=strlen("Fetching nodes ||");
        *ptr='.'; //replace \0 char


        float oldpercent = percentFetchnodes;
        percentFetchnodes =  request->getTransferredBytes()*1.0/request->getTotalBytes()*100.0;
        if (percentFetchnodes==oldpercent && oldpercent!=0) return;
        if (percentFetchnodes <0) percentFetchnodes = 0;

        char aux[40];
        if (request->getTotalBytes()<0) return; // after a 100% this happens
        if (request->getTransferredBytes()<0.001*request->getTotalBytes()) return; // after a 100% this happens
        sprintf(aux,"||(%lld/%lld MB: %.2f %%) ",request->getTransferredBytes()/1024/1024,request->getTotalBytes()/1024/1024,percentFetchnodes);
        sprintf(outputString+cols-strlen(aux),"%s",aux);
        for (int i=0; i<= (cols-strlen("Fetching nodes ||")-strlen(aux))*1.0*percentFetchnodes/100.0; i++) *ptr++='#';
        {
            if (RL_ISSTATE(RL_STATE_INITIALIZED))
            {
                rl_message("%s",outputString);
            }
            else
            {
                cout << outputString << endl; //too verbose
            }
        }

#endif

        break;

    }
    default:
        LOG_debug << "onRequestUpdate of unregistered type of request: " << request->getType();
        break;
    }
}

void MegaCmdListener::onRequestTemporaryError(MegaApi *api, MegaRequest *request, MegaError* e){

}


MegaCmdListener::~MegaCmdListener(){

}

MegaCmdListener::MegaCmdListener(MegaApi *megaApi, MegaRequestListener *listener)
{
    this->megaApi=megaApi;
    this->listener=listener;
}




////////////////////////////////////////
///      MegaCmdListener methods     ///
////////////////////////////////////////

void MegaCmdTransferListener::onTransferStart(MegaApi* api, MegaTransfer *Transfer){
    if (!Transfer)
    {
        LOG_err << " onTransferStart for undefined Transfer ";
        return;
    }

    LOG_verbose << "onTransferStart Transfer->getType(): " << Transfer->getType();

//    switch(Transfer->getType())
//    {
//        default:
//            LOG_debug << "onTransferStart of unregistered type of Transfer: " << Transfer->getType();
//            break;
//    }

    //clear_display();
}

void MegaCmdTransferListener::doOnTransferFinish(MegaApi* api, MegaTransfer *transfer, MegaError* e)
{
    if (!transfer)
    {
        LOG_err << " onTransferFinish for undefined transfer ";
        return;
    }

    LOG_verbose << "onTransferFinish Transfer->getType(): " << transfer->getType();
}


void MegaCmdTransferListener::onTransferUpdate(MegaApi* api, MegaTransfer *Transfer){
    if (!Transfer)
    {
        LOG_err << " onTransferUpdate for undefined Transfer ";
        return;
    }

    LOG_verbose << "onTransferUpdate Transfer->getType(): " << Transfer->getType();


}

void MegaCmdTransferListener::onTransferTemporaryError(MegaApi *api, MegaTransfer *transfer, MegaError* e){

}


MegaCmdTransferListener::~MegaCmdTransferListener(){

}

MegaCmdTransferListener::MegaCmdTransferListener(MegaApi *megaApi, MegaTransferListener *listener)
{
    this->megaApi=megaApi;
    this->listener=listener;
}


bool MegaCmdTransferListener::onTransferData(MegaApi *api, MegaTransfer *transfer, char *buffer, size_t size){

}


//TreeProcCopy::TreeProcCopy()
//{
//    nn = NULL;
//    nc = 0;
//}

//void TreeProcCopy::allocnodes()
//{
//    nn = new NewNode[nc];
//}

//TreeProcCopy::~TreeProcCopy()
//{
//    delete[] nn;
//}

//// determine node tree size (nn = NULL) or write node tree to new nodes array
//void TreeProcCopy::proc(MegaClient* client, Node* n)
//{
//    if (nn)
//    {
//        string attrstring;
//        SymmCipher key;
//        NewNode* t = nn + --nc;

//        // copy node
//        t->source = NEW_NODE;
//        t->type = n->type;
//        t->nodehandle = n->nodehandle;
//        t->parenthandle = n->parent->nodehandle;

//        // copy key (if file) or generate new key (if folder)
//        if (n->type == FILENODE)
//        {
//            t->nodekey = n->nodekey;
//        }
//        else
//        {
//            byte buf[FOLDERNODEKEYLENGTH];
//            PrnGen::genblock(buf, sizeof buf);
//            t->nodekey.assign((char*) buf, FOLDERNODEKEYLENGTH);
//        }

//        key.setkey((const byte*) t->nodekey.data(), n->type);

//        n->attrs.getjson(&attrstring);
//        t->attrstring = new string;
//        client->makeattr(&key, t->attrstring, attrstring.c_str());
//    }
//    else
//    {
//        nc++;
//    }
//}

int loadfile(string* name, string* data)
{
    //TODO: modify using API
//    FileAccess* fa = client->fsaccess->newfileaccess();

//    if (fa->fopen(name, 1, 0))
//    {
//        data->resize(fa->size);
//        fa->fread(data, data->size(), 0, 0);
//        delete fa;

//        return 1;
//    }

//    delete fa;

    return 0;
}

//void xferq(direction_t d, int cancel)
//{
//    string name;

//    for (appfile_list::iterator it = appxferq[d].begin(); it != appxferq[d].end(); )
//    {
//        if (cancel < 0 || cancel == (*it)->seqno)
//        {
//            (*it)->displayname(&name);

//            OUTSTREAM << (*it)->seqno << ": " << name;

//            if (d == PUT)
//            {
//                AppFilePut* f = (AppFilePut*) *it;

//                OUTSTREAM << " -> ";

//                if (f->targetuser.size())
//                {
//                    OUTSTREAM << f->targetuser << ":";
//                }
//                else
//                {
//                    string path;
//                    nodepath(f->h, &path);
//                    OUTSTREAM << path;
//                }
//            }

//            if ((*it)->transfer && (*it)->transfer->slot)
//            {
//                OUTSTREAM << " [ACTIVE]";
//            }
//            OUTSTREAM << endl;

//            if (cancel >= 0)
//            {
//                OUTSTREAM << "Canceling..." << endl;

//                if ((*it)->transfer)
//                {
//                    client->stopxfer(*it);
//                }
//                delete *it++;
//            }
//            else
//            {
//                it++;
//            }
//        }
//        else
//        {
//            it++;
//        }
//    }
//}

void delete_finished_threads()
{
    for(std::vector<MegaThread *>::iterator it = petitionThreads.begin(); it != petitionThreads.end();) {
        /* std::cout << *it; ... */
        MegaThread *mt = (MegaThread *)*it;
#ifdef USE_QT
        if (mt->isFinished())
        {
            delete mt;
            it=petitionThreads.erase(it);
        }
        else
#endif
            ++it;
    }
}

void finalize()
{
    LOG_info << "closing application ..." ;
    delete_finished_threads();
    delete cm;
    delete console;
    delete api;
    while (!apiFolders.empty())
    {
        delete apiFolders.front();
        apiFolders.pop();
    }
    for (std::vector< MegaApi * >::iterator it = occupiedapiFolders.begin() ; it != occupiedapiFolders.end(); ++it)
    {
        delete (*it);
    }
    occupiedapiFolders.clear();

    delete loggerCMD;
    delete megaCmdGlobalListener;
    delete fsAccessCMD;

    OUTSTREAM << "resources have been cleaned ..."  << endl;

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
//        finalize();
//        delete console;
        exit(0);
    }

    if (*l && prompt == COMMAND)
    {
        add_history(l);
    }

    line = l;
}

void actUponGetExtendedAccountDetails(SynchronousRequestListener *srl,int timeout=-1)
{
    if (timeout==-1)
        srl->wait();
    else
    {
        int trywaitout=srl->trywait(timeout);
        if (trywaitout){
           LOG_err << "GetExtendedAccountDetails took too long, it may have failed. No further actions performed";
           return;
        }
    }

    if (srl->getError()->getErrorCode() == MegaError::API_OK)
    {
        char timebuf[32], timebuf2[32];

        LOG_verbose << "actUponGetExtendedAccountDetails ok";

        MegaAccountDetails *details =  srl->getRequest()->getMegaAccountDetails();
        if (details)
        {
            OUTSTREAM << "\tAvailable storage: " << details->getStorageMax() << " byte(s)" << endl;
            MegaNode *n = api->getRootNode();
            OUTSTREAM << "\t\tIn ROOT: " << details->getStorageUsed(n->getHandle()) << " byte(s) in "
                 << details->getNumFiles(n->getHandle())  << " file(s) and " << details->getNumFolders(n->getHandle()) << " folder(s)" << endl;
            delete n;

            n = api->getInboxNode();
            OUTSTREAM << "\t\tIn INBOX: " << details->getStorageUsed(n->getHandle()) << " byte(s) in "
                 << details->getNumFiles(n->getHandle())  << " file(s) and " << details->getNumFolders(n->getHandle()) << " folder(s)" << endl;
            delete n;

            n = api->getRubbishNode();
            OUTSTREAM << "\t\tIn RUBBISH: " << details->getStorageUsed(n->getHandle()) << " byte(s) in "
                 << details->getNumFiles(n->getHandle())  << " file(s) and " << details->getNumFolders(n->getHandle()) << " folder(s)" << endl;
            delete n;


            MegaNodeList *inshares = api->getInShares();
            if (inshares)
            for (int i=0; i<inshares->size();i++)
            {
                n=inshares->get(i);
                OUTSTREAM << "\t\tIn INSHARE "<< n->getName() << ": " << details->getStorageUsed(n->getHandle()) << " byte(s) in "
                     << details->getNumFiles(n->getHandle())  << " file(s) and " << details->getNumFolders(n->getHandle()) << " folder(s)" << endl;
            }
            delete inshares;

//            if (details->getTransferMax())
//            {
//                OUTSTREAM << "\tTransfer completed: " << details->getTransferOwnUsed() << " of " << details->getTransferMax() << "("
//                     << ( 100 * details->getTransferOwnUsed() / details->getTransferMax() ) << "%)" << endl;

//            }

//            OUTSTREAM << "\tTransfer history:\n";
//            MegaTransferList *transferlist = api->getTransfers();
//            if (transferlist)
//            {
//                for (int i=0;i<transferlist->size();i++)
//                {
//                    MegaTransfer * transfer = transferlist->get(i);
//                    OUTSTREAM << "\t\t" << transfer->getTransferredBytes() << " OUTSTREAM of " << transfer->getTotalBytes() << " bytes downloaded up to "<< transfer->getUpdateTime() << endl;
//                }
//            }
//            delete transferlist;



            OUTSTREAM << "\tPro level: " << details->getProLevel() << endl;
            if (details->getProLevel())
            {
                if (details->getProExpiration())
                {
                    time_t ts = details->getProExpiration();
                    strftime(timebuf, sizeof timebuf, "%c", localtime(&ts));
                    printf("\t\tPro expiration date: %s\n", timebuf);
                }
            }
            char * subscriptionMethod = details->getSubscriptionMethod();
            OUTSTREAM << "\tSubscription type: " << subscriptionMethod << endl;
            delete []subscriptionMethod;
            OUTSTREAM << "\tAccount balance:" << endl;
            for (int i = 0; i < details->getNumBalances();i++)
            {
                MegaAccountBalance * balance =  details->getBalance(i);
                printf("\tBalance: %.3s %.02f\n", balance->getCurrency(), balance->getAmount());
            }

            if (details->getNumPurchases())
            {
                OUTSTREAM << "Purchase history:" << endl;
                for (int i = 0; i < details->getNumPurchases(); i++)
                {
                    MegaAccountPurchase *purchase = details->getPurchase(i);

                    time_t ts = purchase->getTimestamp();
                    strftime(timebuf, sizeof timebuf, "%c", localtime(&ts));
                    printf("\tID: %.11s Time: %s Amount: %.3s %.02f Payment method: %d\n",
                           purchase->getHandle(), timebuf, purchase->getCurrency(), purchase->getAmount(), purchase->getMethod());
                }
            }

            if (details->getNumTransactions())
            {
                for (int i = 0; i < details->getNumTransactions(); i++)
                {
                    MegaAccountTransaction *transaction = details->getTransaction(i);

                    OUTSTREAM << "Transaction history:" << endl;

                        time_t ts = transaction->getTimestamp();
                        strftime(timebuf, sizeof timebuf, "%c", localtime(&ts));
                        printf("\tID: %.11s Time: %s Amount: %.3s %.02f\n",
                               transaction->getHandle(), timebuf, transaction->getCurrency(), transaction->getAmount());
                }
            }

            int alive_sessions = 0;
            OUTSTREAM << "Current Active Sessions:" << endl;
            char sdetails[500];
            for (int i = 0; i < details->getNumSessions(); i++)
            {
                MegaAccountSession * session = details->getSession(i);
                if (session->isAlive())
                {
                    time_t ts = session->getCreationTimestamp();
                    strftime(timebuf, sizeof timebuf, "%c", localtime(&ts));
                    ts = session->getMostRecentUsage();
                    strftime(timebuf2, sizeof timebuf, "%c", localtime(&ts));


                    MegaHandle id = session->getHandle();
                    char sid[12];
                    Base64::btoa((byte*)&(id), sizeof(id), sid);


                    if (session->isCurrent())
                    {
                        sprintf(sdetails,"\t* Current Session\n");
                    }
                    char * userAgent = session->getUserAgent();
                    char * country = session->getCountry();
                    char * ip = session->getIP();

                    sprintf(sdetails,"\tSession ID: %s\n\tSession start: %s\n\tMost recent activity: %s\n\tIP: %s\n\tCountry: %.2s\n\tUser-Agent: %s\n\t-----\n",
                           sid,
                           timebuf,
                           timebuf2,
                           ip,
                           country,
                           userAgent
                           );
                    OUTSTREAM << sdetails;
                    delete []userAgent;
                    delete []country;
                    delete []ip;
                    alive_sessions++;
                }
                delete session;
            }
            if (alive_sessions)
                OUTSTREAM << details->getNumSessions() << " active sessions opened" << endl;
        delete details;
        }
    }
    else
    {
        LOG_err << " failed to GetExtendedAccountDetails. Error: " << srl->getError()->getErrorString();
    }
}

void actUponFetchNodes(SynchronousRequestListener *srl,int timeout=-1)
{
    if (timeout==-1)
        srl->wait();
    else
    {
        int trywaitout=srl->trywait(timeout);
        if (trywaitout){
           LOG_err << "Fetch nodes took too long, it may have failed. No further actions performed";
           return;
        }
    }

    if (srl->getError()->getErrorCode() == MegaError::API_OK)
    {
        LOG_verbose << "actUponFetchNodes ok";

        MegaNode *cwdNode = (cwd==UNDEF)?NULL:api->getNodeByHandle(cwd);
        if (cwd == UNDEF || ! cwdNode)
        {
            MegaNode *rootNode = srl->getApi()->getRootNode();
            cwd = rootNode->getHandle();
            delete rootNode;
        }
        if (cwdNode) delete cwdNode;
        LOG_debug << " Fetch nodes correctly";
    }
    else
    {
        LOG_err << " failed to fetch nodes. Error: " << srl->getError()->getErrorString();
    }
}


void actUponLogin(SynchronousRequestListener *srl,int timeout=-1)
{
    if (timeout==-1)
        srl->wait();
    else
    {
        int trywaitout=srl->trywait(timeout);
        if (trywaitout){
           LOG_err << "Login took too long, it may have failed. No further actions performed";
           return;
        }
    }

    LOG_debug << "actUponLogin login";

    if (srl->getRequest()->getEmail())
    {
        LOG_debug << "actUponLogin login email: " << srl->getRequest()->getEmail();
    }

    if (srl->getError()->getErrorCode() == MegaError::API_ENOENT) // failed to login
    {
        LOG_err << "Login failed: invalid email or password";
    }
    else if (srl->getError()->getErrorCode() == MegaError::API_EINCOMPLETE)
    {
        LOG_err << "Login failed: unconfirmed account. Please confirm your account";
    }
    else if(srl->getError()->getErrorCode() == MegaError::API_OK) //login success:
    {
        LOG_info << "Login correct ... " << srl->getRequest()->getEmail();

        session = srl->getApi()->dumpSession();
        ConfigurationManager::saveSession(session);
        srl->getApi()->fetchNodes(srl);
        actUponFetchNodes(srl,timeout);//TODO: should more accurately be max(0,timeout-timespent)
    }
    else //TODO: complete error control
    {
        LOG_err << "Login failed: " << srl->getError()->getErrorString();
    }
}

void actUponLogout(SynchronousRequestListener *srl,int timeout=0)
{
    if (!timeout)
        srl->wait();
    else
    {
        int trywaitout=srl->trywait(timeout);
        if (trywaitout){
           LOG_err << "Logout took too long, it may have failed. No further actions performed";
           return;
        }
    }
    if (srl->getError()->getErrorCode() == MegaError::API_OK) // failed to login
    {
        LOG_verbose << "actUponLogout logout ok";
        cwd = UNDEF;
        delete []session;
        session=NULL;
    }
    else
    {
        LOG_err << "actUponLogout failed to logout: " << srl->getError()->getErrorString();
    }
}

int actUponCreateFolder(SynchronousRequestListener *srl,int timeout=0)
{
    if (!timeout)
        srl->wait();
    else
    {
        int trywaitout=srl->trywait(timeout);
        if (trywaitout){
           LOG_err << "actUponCreateFolder took too long, it may have failed. No further actions performed";
           return 1;
        }
    }
    if (srl->getError()->getErrorCode() == MegaError::API_OK)
    {
        LOG_verbose << "actUponCreateFolder Create Folder ok";
        return 0;
    }
    else
    {
        if (srl->getError()->getErrorCode() == MegaError::API_EACCESS)
        {
            LOG_err << "actUponCreateFolder failed to create folder: Access Denied";
        }
        else
        {
            LOG_err << "actUponCreateFolder failed to create folder: " << srl->getError()->getErrorString();
        }
        return 2;
    }
}



int actUponDeleteNode(SynchronousRequestListener *srl,int timeout=0)
{
    if (!timeout)
        srl->wait();
    else
    {
        int trywaitout=srl->trywait(timeout);
        if (trywaitout){
           LOG_err << "delete took too long, it may have failed. No further actions performed";
           return 1;
        }
    }
    if (srl->getError()->getErrorCode() == MegaError::API_OK) // failed to login
    {
        LOG_verbose << "actUponDeleteNode delete ok";
        return 0;
    }
    else
    {
        if (srl->getError()->getErrorCode() == MegaError::API_EACCESS)
        {
            LOG_err << "actUponDeleteNode failed to delete: Access Denied";
        }
        else
        {
            LOG_err << "actUponDeleteNode failed to delete: " << srl->getError()->getErrorString();
        }
        return 2;
    }
}



// trim from start
static inline std::string &ltrim(std::string &s, const char &c) {
    size_t pos = s.find_first_not_of(c);
    s=s.substr(pos==string::npos?s.length():pos,s.length());
    return s;
}

// trim at the end
static inline std::string &rtrim(std::string &s, const char &c) {
    size_t pos = s.find_last_of(c);
    size_t last=pos==string::npos?s.length():pos;
    if (last < s.length()-1)
    {
        if (s.at(last+1) != c)
        {
            last = s.length();
        }
    }

    s=s.substr(0,last);
    return s;
}

//
bool setOptionsAndFlags(map<string,string> *opt,map<string,int> *flags,vector<string> *ws, set<string> vvalidOptions, bool global=false)
{
    bool discarded = false;
    //delete finished threads
//    set<string> vvalidOptions(validOptions,validOptions + sizeof(validOptions)/sizeof(*validOptions));

    for(std::vector<string>::iterator it = ws->begin(); it != ws->end();) {
        /* std::cout << *it; ... */
        string w = (string)*it;
        if (w.length() && w.at(0)=='-') //begins with "-"
        {
            if (w.length()>1 && w.at(1)!='-'){ //single character flags!
                for (uint i=1;i<w.length();i++)
                {
                    string optname = w.substr(i,1);
                    if (vvalidOptions.find(optname) !=vvalidOptions.end())
                    {
                        (*flags)[optname]=(flags->count(optname)?(*flags)[optname]:0) + 1;
                    }
                    else
                    {
                        LOG_err << "Invalid argument: "<< optname;
                        discarded = true;
                    }
                }
            }
            else if (w.find_first_of("=") == std::string::npos) //flag
            {
                string optname = ltrim(w,'-');
                if (vvalidOptions.find(optname) !=vvalidOptions.end())
                {
                    (*flags)[optname]=(flags->count(optname)?(*flags)[optname]:0) + 1;
                }
                else
                {
                    LOG_err << "Invalid argument: "<< optname;
                    discarded = true;
                }
            }
            it=ws->erase(it);
        }
        else //not an option/flag
        {
            if (global)
                return discarded; //leave the others
            ++it;
        }
    }
    return discarded;
}



//void setOptionsAndFlags(map<string,string> *opt,map<string,int> *flags,vector<string> *ws, const char *validOptions[], bool discard = false)
//{
//    //delete finished threads
//    set<string> vvalidOptions(validOptions,validOptions + sizeof(validOptions)/sizeof(*validOptions));

//    for(std::vector<string>::iterator it = ws->begin(); it != ws->end();) {
//        /* std::cout << *it; ... */
//        string w = (string)*it;
//        if (w.length() && w.at(0)=='-') //begins with "-"
//        {
//            if (w.length()>1 && w.at(1)!='-'){ //single character flags!
//                for (int i=1;i<w.length();i++)
//                {
//                    string optname = w.substr(i,1);
//                    if (vvalidOptions.find(optname) !=vvalidOptions.end())
//                    {
//                        (*flags)[optname]=(flags->count(optname)?(*flags)[optname]:0) + 1;
//                    }
//                    else if (discard)
//                    {
//                        LOG_warn << " Option invalid (discarded): "<< optname;
//                        w.replace(i,1,"_");
//                    }
//                }
//                ++it;
//            }
//            else if (w.find_first_of("=") == std::string::npos) //flag
//            {
//                string optname = ltrim(w,'-');
//                if (vvalidOptions.find(optname) !=vvalidOptions.end())
//                {
//                    (*flags)[optname]=(flags->count(optname)?(*flags)[optname]:0) + 1;
//                    it=ws->erase(it);
//                }
//                else if (discard)
//                {
//                    LOG_warn << " Option invalid (discarded): "<< optname;
//                    it=ws->erase(it);
//                }
//                else
//                    ++it;
//            }
//        }
//        else
//        {
////            return; //leave the others
//            ++it;
//        }
//    }
//}

int getFlag(map<string,int> *flags, const char * optname)
{
    return flags->count(optname)?(*flags)[optname]:0;
}

int getLinkType(string link){
    int posHash=link.find_first_of("#");
    if (posHash==string::npos || !(posHash+1 < link.length() )) return MegaNode::TYPE_UNKNOWN;
    if (link.at(posHash+1)=='F') return MegaNode::TYPE_FOLDER;
    return MegaNode::TYPE_FILE;
}

bool isPublicLink(string link){
    if (link.find_first_of("#") == 0 && link.find_first_of("#") != string::npos ) return true;
    return false;
}

bool isFolder(string path){
//TODO: move to MegaFileSystemAccess

    struct stat path_stat;
    stat(path.c_str(), &path_stat);
    return S_ISDIR(path_stat.st_mode);
}

bool isRegularFile(string path)
{
    struct stat path_stat;
    stat(path.c_str(), &path_stat);
    return S_ISREG(path_stat.st_mode);
}

bool pathExits(string path){//TODO: move to MegaFileSystemAccess
//    return access( path, F_OK ) != -1 ;
    struct stat path_stat;
    int ret=stat(path.c_str(), &path_stat);
    return ret==0;
}

void downloadNode(string localPath, MegaApi* api, MegaNode *node)
{
    MegaCmdTransferListener *megaCmdTransferListener = new MegaCmdTransferListener(api,NULL);
    LOG_debug << "Starting download: " << node->getName() << " to : " << localPath;
    api->startDownload(node,localPath.c_str(),megaCmdTransferListener);
    megaCmdTransferListener->wait();
    //TODO: process errors
    LOG_info << "Download complete: " << localPath << megaCmdTransferListener->getTransfer()->getFileName();
    delete megaCmdTransferListener;
}
void uploadNode(string localPath, MegaApi* api, MegaNode *node)
{
    MegaCmdTransferListener *megaCmdTransferListener = new MegaCmdTransferListener(api,NULL);
    LOG_debug << "Starting download: " << node->getName() << " to : " << localPath;
    api->startUpload(localPath.c_str(),node,megaCmdTransferListener);
    megaCmdTransferListener->wait();
    //TODO: process errors
    char * destinyPath=api->getNodePath(node);
    LOG_info << "Upload complete: " << megaCmdTransferListener->getTransfer()->getFileName() << " to " << destinyPath;
    delete []destinyPath;
    delete megaCmdTransferListener;
}



// execute command
static void process_line(char* l)
{
    switch (prompt)
    {
        case LOGINPASSWORD:
        {
        //TODO: modify using API
//            client->pw_key(l, pwkey);

//            if (signupcode.size())
//            {
//                // verify correctness of supplied signup password
//                SymmCipher pwcipher(pwkey);
//                pwcipher.ecb_decrypt(signuppwchallenge);

//                if (MemAccess::get<int64_t>((const char*)signuppwchallenge + 4))
//                {
//                    OUTSTREAM << endl << "Incorrect password, please try again." << endl;
//                }
//                else
//                {
//                    // decrypt and set master key, then proceed with the confirmation
//                    pwcipher.ecb_decrypt(signupencryptedmasterkey);
//                    //TODO: modify using API
////                    client->key.setkey(signupencryptedmasterkey);

////                    client->confirmsignuplink((const byte*) signupcode.data(), signupcode.size(),
////                                              MegaClient::stringhash64(&signupemail, &pwcipher));
//                }

//                signupcode.clear();
//            }
//            else
//            {
                //TODO: modify using API
//                client->login(login.c_str(), pwkey);
                  MegaCmdListener *megaCmdListener = new MegaCmdListener(api,NULL);
                  api->login(login.c_str(), l,megaCmdListener);
                  actUponLogin(megaCmdListener);
                  delete megaCmdListener;
//            }

            setprompt(COMMAND);
            return;
        }

        case OLDPASSWORD:
        //TODO: modify using API
//            client->pw_key(l, pwkeybuf);

            if (!memcmp(pwkeybuf, pwkey, sizeof pwkey))
            {
                OUTSTREAM << endl;
                setprompt(NEWPASSWORD);
            }
            else
            {
                OUTSTREAM << endl << "Bad password, please try again" << endl;
                setprompt(COMMAND);
            }
            return;

        case NEWPASSWORD:
        //TODO: modify using API
//            client->pw_key(l, newpwkey);

            OUTSTREAM << endl;
            setprompt(PASSWORDCONFIRM);
            return;

        case PASSWORDCONFIRM:
        //TODO: modify using API
//            client->pw_key(l, pwkeybuf);

            if (memcmp(pwkeybuf, newpwkey, sizeof pwkey))
            {
                OUTSTREAM << endl << "Mismatch, please try again" << endl;
            }
            else
            {
//                error e;

                if (signupemail.size())
                {
                    //TODO: modify using API
//                    client->sendsignuplink(signupemail.c_str(), signupname.c_str(), newpwkey);
                }
                else
                {
                    //TODO: modify using API
//                    if ((e = client->changepw(pwkey, newpwkey)) == API_OK)
//                    {
//                        memcpy(pwkey, newpwkey, sizeof pwkey);
//                        OUTSTREAM << endl << "Changing password..." << endl;
//                    }
//                    else
//                    {
//                        OUTSTREAM << "You must be logged in to change your password." << endl;
//                    }
                }
            }

            setprompt(COMMAND);
            signupemail.clear();
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

            MegaNode* n;

            if (words[0] == "?" || words[0] == "h" || words[0] == "help")
            {
                OUTSTREAM << "      " << getUsageStr("login") << endl;
                OUTSTREAM << "      " << getUsageStr("begin") << endl;
                OUTSTREAM << "      " << getUsageStr("signup") << endl;
                OUTSTREAM << "      " << getUsageStr("confirm") << endl;
                OUTSTREAM << "      " << getUsageStr("session") << endl;
                OUTSTREAM << "      " << getUsageStr("mount") << endl;
                OUTSTREAM << "      " << getUsageStr("ls") << endl;
                OUTSTREAM << "      " << getUsageStr("cd") << endl;
                OUTSTREAM << "      " << getUsageStr("log") << endl;
                OUTSTREAM << "      " << getUsageStr("pwd") << endl;
                OUTSTREAM << "      " << getUsageStr("lcd") << endl;
                OUTSTREAM << "      " << getUsageStr("lpwd") << endl;
                OUTSTREAM << "      " << getUsageStr("import") << endl;
                OUTSTREAM << "      " << getUsageStr("put") << endl;
                OUTSTREAM << "      " << getUsageStr("putq") << endl;
                OUTSTREAM << "      " << getUsageStr("get") << endl;
                OUTSTREAM << "      " << getUsageStr("getq") << endl;
                OUTSTREAM << "      " << getUsageStr("pause") << endl;
                OUTSTREAM << "      " << getUsageStr("getfa") << endl;
                OUTSTREAM << "      " << getUsageStr("mkdir") << endl;
                OUTSTREAM << "      " << getUsageStr("rm") << endl;
                OUTSTREAM << "      " << getUsageStr("mv") << endl;
                OUTSTREAM << "      " << getUsageStr("cp") << endl;
                #ifdef ENABLE_SYNC
                OUTSTREAM << "      " << getUsageStr("sync") << endl;
                #endif
                OUTSTREAM << "      " << getUsageStr("export") << endl;
                OUTSTREAM << "      " << getUsageStr("share") << endl;
                OUTSTREAM << "      " << getUsageStr("invite") << endl;
                OUTSTREAM << "      " << getUsageStr("ipc") << endl;
                OUTSTREAM << "      " << getUsageStr("showpcr") << endl;
                OUTSTREAM << "      " << getUsageStr("users") << endl;
                OUTSTREAM << "      " << getUsageStr("getua") << endl;
                OUTSTREAM << "      " << getUsageStr("putua") << endl;
                OUTSTREAM << "      " << getUsageStr("putbps") << endl;
                OUTSTREAM << "      " << getUsageStr("killsession") << endl;
                OUTSTREAM << "      " << getUsageStr("whoami") << endl;
                OUTSTREAM << "      " << getUsageStr("passwd") << endl;
                OUTSTREAM << "      " << getUsageStr("retry") << endl;
                OUTSTREAM << "      " << getUsageStr("recon") << endl;
                OUTSTREAM << "      " << getUsageStr("reload") << endl;
                OUTSTREAM << "      " << getUsageStr("logout") << endl;
                OUTSTREAM << "      " << getUsageStr("locallogout") << endl;
                OUTSTREAM << "      " << getUsageStr("symlink") << endl;
                OUTSTREAM << "      " << getUsageStr("version") << endl;
                OUTSTREAM << "      " << getUsageStr("debug") << endl;
                #ifdef ENABLE_CHAT
                OUTSTREAM << "      " << getUsageStr("chatf") << endl;
                OUTSTREAM << "      " << getUsageStr("chatc") << endl;
                OUTSTREAM << "      " << getUsageStr("chati") << endl;
                OUTSTREAM << "      " << getUsageStr("chatr") << endl;
                OUTSTREAM << "      " << getUsageStr("chatu") << endl;
                OUTSTREAM << "      " << getUsageStr("chatga") << endl;
                OUTSTREAM << "      " << getUsageStr("chatra") << endl;
                #endif
                OUTSTREAM << "      " << getUsageStr("quit") << endl;

                return;
            }

            map<string,string> cloptions;
            map<string,int> clflags;

            string validGlobalParameters[]={"v","help"};
            set<string> validParams(validGlobalParameters,validGlobalParameters + sizeof(validGlobalParameters)/sizeof(*validGlobalParameters));
            if (setOptionsAndFlags(&cloptions,&clflags,&words,validParams,true) ) return;

            string thecommand = words[0];

            if ("ls" == thecommand)
            {
                validParams.insert("R");
                validParams.insert("r");
                validParams.insert("l");
            }
            else if ("log" == thecommand)
            {
                validParams.insert("c");
                validParams.insert("s");
            }else if ("sync" == thecommand)
            {
                validParams.insert("d");
                validParams.insert("s");
            }

            if (!validCommand(thecommand)) { //unknown command
                OUTSTREAM << "      " << getUsageStr(thecommand.c_str()) << endl;
                return;
            }

            if (setOptionsAndFlags(&cloptions,&clflags,&words,validParams) ) return;

            setCurrentThreadLogLevel(MegaApi::LOG_LEVEL_ERROR+getFlag(&clflags,"v"));

            if(getFlag(&clflags,"help")) {
                string h = getHelpStr(thecommand.c_str()) ;
                 OUTSTREAM << h << endl;
                 return;
            }


                    if (words[0] == "ls")
                    {
                        if (!api->isLoggedIn()) { LOG_err << "Not logged in"; return;}
//                        int recursive = words.size() > 1 && words[1] == "-R";
                        int recursive = getFlag(&clflags,"R") + getFlag(&clflags,"r") ;
                        int extended_info = getFlag(&clflags,"l");

                        if ((int) words.size() > 1)
                        {
                            string rNpath = "NULL";
                            MegaNode *rN = NULL;
                            string cwpath;
                            if (words[1].find('/') != string::npos)
                            {
                                nodepath(cwd, &cwpath);
                                if (words[1].find_first_of(cwpath)  == 0 )
                                {
                                    rNpath = "";
                                }
                                else
                                {
                                    rNpath = cwpath;
                                }
                            }

                                if (words[1].find('*')!=string::npos || words[1].find('?')!=string::npos)// || words[1].find('/')!=string::npos)
                                {
                                    vector<MegaNode *> *nodesToList = nodesbypath(words[1].c_str());
                                    if (nodesToList)
                                    {
                                        for (std::vector< MegaNode * >::iterator it = nodesToList->begin() ; it != nodesToList->end(); ++it)
                                        {
                                            MegaNode * n = *it;
                                            if (n)
                                            {
                                                dumptree(n, recursive, extended_info, 1,rNpath);
                                                delete n;
                                            }
                                        }
                                        nodesToList->clear();
                                        delete nodesToList ;
                                    }

                                }
                                else
                                {
                                    n = nodebypath(words[1].c_str());
                                    if (n)
                                    {
                                        dumptree(n, recursive, extended_info, 1,rNpath);
                                        delete n;
                                    }
                                }

                                delete rN;
    //                            delete rNpath;
                            }
                            else
                            {
                                n = api->getNodeByHandle(cwd);
                                if (n)
                                {
                                    dumptree(n, recursive, extended_info);
                                    delete n;
                                }
                            }



                            return;
                        }
                        else if (words[0] == "cd")
                        {
                            if (!api->isLoggedIn()) { LOG_err << "Not logged in"; return; }
                            if (words.size() > 1)
                            {
                                if ((n = nodebypath(words[1].c_str())))
                                {
                                    if (n->getType() == MegaNode::TYPE_FILE)
                                    {
                                        LOG_err << words[1] << ": Not a directory";
                                    }
                                    else
                                    {
                                        cwd = n->getHandle();
                                    }
                                    delete n;
                                }
                                else
                                {
                                    LOG_err << words[1] << ": No such file or directory";
                                }
                            }
                            else
                            {
                                MegaNode * rootNode = api->getRootNode();
                                if (!rootNode) {
                                    LOG_err << "nodes not fetched";
                                    delete rootNode;
                                    return;
                                }
                                cwd = rootNode->getHandle();
                                delete rootNode;
                            }

                            return;
                        }
                        else if (words[0] == "rm")
                        {
                            if (words.size() > 1)
                            {
                                for (uint i = 1; i < words.size(); i++ )
                                {
                                    if (words[i].find('*')!=string::npos || words[i].find('?')!=string::npos)
                                    {
                                        vector<MegaNode *> *nodesToDelete = nodesbypath(words[i].c_str());
                                        for (std::vector< MegaNode * >::iterator it = nodesToDelete->begin() ; it != nodesToDelete->end(); ++it)
                                        {
                                            MegaNode * nodeToDelete = *it;
                                            if (nodeToDelete)
                                            {
                                                LOG_verbose << "Deleting recursively: " << words[i];
                                                MegaCmdListener *megaCmdListener = new MegaCmdListener(api,NULL);
                                                api->remove(nodeToDelete, megaCmdListener);
                                                actUponDeleteNode(megaCmdListener);
                                                delete nodeToDelete;
                                            }
                                        }
                                        nodesToDelete->clear();
                                        delete nodesToDelete ;
                                    }
                                    else
                                    {

                                        MegaNode * nodeToDelete = nodebypath(words[i].c_str());
                                        if (nodeToDelete)
                                        {
                                            LOG_verbose << "Deleting recursively: " << words[i];
                                            MegaCmdListener *megaCmdListener = new MegaCmdListener(api,NULL);
                                            api->remove(nodeToDelete, megaCmdListener);
                                            actUponDeleteNode(megaCmdListener);
                                            delete nodeToDelete;
                                            delete megaCmdListener;
                                        }
                                    }

                                }
                            }
                            else
                            {
                                OUTSTREAM << "      rm remotepath" << endl;
                            }

                            return;
                        }
                        else if (words[0] == "mv")
                        {
                            MegaNode* tn; //target node
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
                                        if (tn->getHandle() == n->getHandle())
                                        {
                                            LOG_err << "Source and destiny are the same";
                                        }
                                        else
                                        {
                                            if (newname.size()) //target not found, but tn has what was before the last "/" in the path.
                                            {
                                                if (tn->getType() == MegaNode::TYPE_FILE)
                                                {
                                                    OUTSTREAM << words[2] << ": Not a directory" << endl;
                                                    delete tn;
                                                    delete n;
                                                    return;
                                                }
                                                else //move and rename!
                                                {
                                                    MegaCmdListener *megaCmdListener = new MegaCmdListener(api,NULL);
                                                    api->moveNode(n,tn,megaCmdListener);
                                                    megaCmdListener->wait(); // TODO: act upon move. log access denied...
                                                    delete megaCmdListener;
                                                    if (megaCmdListener->getError() && megaCmdListener->getError()->getErrorCode() == MegaError::API_OK)
                                                    {
                                                        MegaCmdListener *megaCmdListener = new MegaCmdListener(api,NULL);
                                                        api->renameNode(n,newname.c_str(),megaCmdListener);
                                                        megaCmdListener->wait(); // TODO: act upon rename. log access denied...
                                                        delete megaCmdListener;
                                                    }
                                                    else
                                                    {
                                                        LOG_err << "Won't rename, since move failed " << n->getName() <<" to " << tn->getName() << " : " << megaCmdListener->getError()->getErrorCode();
                                                    }
                                                }
                                            }
                                            else //target found
                                            {
                                                if (tn->getType() == MegaNode::TYPE_FILE) //move & remove old & rename new
                                                {
                                                    // (there should never be any orphaned filenodes)
                                                    MegaNode *tnParentNode = api->getNodeByHandle(tn->getParentHandle());
                                                    if (tnParentNode )
                                                    {

                                                        delete tnParentNode;

                                                        //move into the parent of target node
                                                        MegaCmdListener *megaCmdListener = new MegaCmdListener(api,NULL);
                                                        api->moveNode(n,api->getNodeByHandle(tn->getParentHandle()),megaCmdListener);
                                                        megaCmdListener->wait(); //TODO: do actuponmove...
                                                        delete megaCmdListener;

                                                        const char* name_to_replace = tn->getName();

                                                        //remove (replaced) target node
                                                        if (n != tn) //just in case moving to same location
                                                        {
                                                            MegaCmdListener *megaCmdListener = new MegaCmdListener(api,NULL);
                                                            api->remove(tn,megaCmdListener); //remove target node
                                                            megaCmdListener->wait(); //TODO: actuponremove ...
                                                            delete megaCmdListener;
                                                            if (megaCmdListener->getError() && megaCmdListener->getError()->getErrorCode() != MegaError::API_OK)
                                                            {
                                                                LOG_err << "Couldnt move " << n->getName() <<" to " << tn->getName() << " : " << megaCmdListener->getError()->getErrorCode();
                                                            }
                                                        }

                                                        // rename moved node with the new name
                                                        if (megaCmdListener->getError() && megaCmdListener->getError()->getErrorCode() == MegaError::API_OK)
                                                        {
                                                            if (!strcmp(name_to_replace,n->getName()))
                                                            {
                                                                MegaCmdListener *megaCmdListener = new MegaCmdListener(api,NULL);
                                                                api->renameNode(n,name_to_replace,megaCmdListener);
                                                                megaCmdListener->wait(); // TODO: act upon rename. log access denied...
                                                                delete megaCmdListener;
                                                            }
                                                        }
                                                        else
                                                        {
                                                            LOG_err << "Won't rename, since move failed " << n->getName() <<" to " << tn->getName() << " : " << megaCmdListener->getError()->getErrorCode();
                                                        }
                                                    }
                                                    else
                                                    {
                                                        LOG_fatal << "Destiny node is orphan!!!";
                                                    }
                                                }
                                                else // target is a folder
                                                {
        //                                            e = client->checkmove(n, tn);
                                                    MegaCmdListener *megaCmdListener = new MegaCmdListener(api,NULL);
                                                    api->moveNode(n,tn,megaCmdListener);
                                                    megaCmdListener->wait();
                                                    delete megaCmdListener;
                                                    //TODO: act upon...
                                                }
                                            }
                                        }
                                        delete tn;
                                    }
                                    else //target not found (not even its folder), cant move
                                    {
                                        OUTSTREAM << words[2] << ": No such directory" << endl;
                                    }
                                    delete n;
                                }
                                else
                                {
                                    OUTSTREAM << words[1] << ": No such file or directory" << endl;
                                }
                            }
                            else
                            {
                                OUTSTREAM << "      mv srcremotepath dstremotepath" << endl;
                            }

                            return;
                        }
                        else if (words[0] == "cp")
                        {
                            MegaNode* tn;
                            string targetuser;
                            string newname;

                            if (words.size() > 2)
                            {
                                if ((n = nodebypath(words[1].c_str())))
                                {
                                    if ((tn = nodebypath(words[2].c_str(), &targetuser, &newname)))
                                    {
                                        if (tn->getHandle() == n->getHandle())
                                        {
                                            LOG_err << "Source and destiny are the same";
                                        }
                                        else
                                        {
                                            if (newname.size()) //target not found, but tn has what was before the last "/" in the path.
                                            {
                                                if (n->getType() == MegaNode::TYPE_FILE)
                                                {
                                                    //copy with new name
                                                    MegaCmdListener *megaCmdListener = new MegaCmdListener(api,NULL);
                                                    api->copyNode(n,tn,newname.c_str(),megaCmdListener); //only works for files
                                                    megaCmdListener->wait();//TODO: actupon...
                                                    delete megaCmdListener;

                                                    //TODO: newname is ignored in case of public node!!!!
                                                }
                                                else//copy & rename
                                                {
                                                    //copy with new name
                                                    MegaCmdListener *megaCmdListener = new MegaCmdListener(api,NULL);
                                                    api->copyNode(n,tn,megaCmdListener);
                                                    megaCmdListener->wait();//TODO: actupon...
                                                    delete megaCmdListener;

                                                    MegaNode * newNode=api->getNodeByHandle(megaCmdListener->getRequest()->getNodeHandle());
                                                    if (newNode)
                                                    {
                                                        MegaCmdListener *megaCmdListener = new MegaCmdListener(api,NULL);
                                                        api->renameNode(newNode,newname.c_str(),megaCmdListener);
                                                        megaCmdListener->wait(); // TODO: act upon rename. log access denied...
                                                        delete megaCmdListener;
                                                        delete newNode;
                                                    }
                                                    else
                                                    {
                                                        LOG_err << " Couldn't find new node created upon cp";
                                                    }
                                                }
                                            }
                                            else
                                            { //target exists
                                                if (tn->getType() == MegaNode::TYPE_FILE)
                                                {
                                                    if (n->getType() == MegaNode::TYPE_FILE)
                                                    {
                                                        // overwrite target if source and target are files
                                                        MegaNode *tnParentNode = api->getNodeByHandle(tn->getParentHandle());
                                                        if (tnParentNode )// (there should never be any orphaned filenodes)
                                                        {
                                                            const char* name_to_replace = tn->getName();
                                                            //copy with new name
                                                            MegaCmdListener *megaCmdListener = new MegaCmdListener(api,NULL);
                                                            api->copyNode(n,tnParentNode,name_to_replace,megaCmdListener);
                                                            megaCmdListener->wait();//TODO: actupon...
                                                            delete megaCmdListener;
                                                            delete tnParentNode;

                                                            //remove target node
                                                            megaCmdListener = new MegaCmdListener(api,NULL);
                                                            api->remove(tn,megaCmdListener);
                                                            megaCmdListener->wait(); //TODO: actuponremove ...
                                                            delete megaCmdListener;
                                                            if (megaCmdListener->getError() && megaCmdListener->getError()->getErrorCode() != MegaError::API_OK)
                                                            {
                                                                LOG_err << "Couldnt delete target node" << tn->getName() << " : " << megaCmdListener->getError()->getErrorCode();
                                                            }
                                                        }
                                                        else
                                                        {
                                                            LOG_fatal << "Destiny node is orphan!!!";
                                                        }
                                                    }
                                                    else
                                                    {
                                                        OUTSTREAM << "Cannot overwrite file with folder" << endl;
                                                        return;
                                                    }
                                                }
                                                else //copying into folder
                                                {
                                                    MegaCmdListener *megaCmdListener = new MegaCmdListener(api,NULL);
                                                    api->copyNode(n,tn,megaCmdListener);
                                                    megaCmdListener->wait();//TODO: actupon...
                                                    delete megaCmdListener;
                                                }
                                            }
                                        }
                                        delete tn;
                                    }
                                    delete n;
                                }
                                else
                                {
                                    OUTSTREAM << words[1] << ": No such file or directory" << endl;
                                }
                            }
                            else
                            {
                                OUTSTREAM << "      cp srcremotepath dstremotepath|dstemail:" << endl;
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
                                    OUTSTREAM << words[1] << ": No such file or directory" << endl;

                                    return;
                                }
                            }
                            else
                            {
                                //TODO: modify using API
    //                            n = client->nodebyhandle(cwd);
                            }

                            if (n)
                            {
                                //TODO: modify using API
    //                            client->proctree(n, &du);

                                OUTSTREAM << "Total storage used: " << (du.numbytes / 1048576) << " MB" << endl;
                                OUTSTREAM << "Total # of files: " << du.numfiles << endl;
                                OUTSTREAM << "Total # of folders: " << du.numfolders << endl;
                            }

                            return;
                        }
                        else if (words[0] == "get")
                        {
                            if (words.size() > 1)
                            {
                                //TODO: modify using API
    //                            if (client->openfilelink(words[1].c_str(), 0) == API_OK)
    //                            {
    //                                OUTSTREAM << "Checking link..." << endl;
    //                                return;
    //                            }
                                string localPath = getCurrentLocalPath()+"/";

                                if (isPublicLink(words[1]))
                                {
                                    if (getLinkType(words[1]) == MegaNode::TYPE_FILE)
                                    {
                                        if (words.size()>2)
                                        {
                                            //TODO: check permissions before download
                                            localPath=words[2];
                                            if (isFolder(localPath)) localPath+="/";
                                            else
                                            {
                                                string containingFolder=localPath.substr(0,localPath.find_last_of("/"));
                                                if(!isFolder(containingFolder))
                                                {
                                                    OUTSTREAM << containingFolder << " is not a valid Download Folder" << endl;
                                                    return;
                                                }
                                            }
                                        }
                                        MegaCmdListener *megaCmdListener = new MegaCmdListener(api,NULL);

                                        api->getPublicNode(words[1].c_str(),megaCmdListener);
                                        megaCmdListener->wait();

                                        if (megaCmdListener->getError() && megaCmdListener->getError()->getErrorCode() != MegaError::API_OK)
                                        {
                                            LOG_err << "Could not get node for link: " << words[1].c_str() << " : " << megaCmdListener->getError()->getErrorCode();
                                            if (megaCmdListener->getError() && megaCmdListener->getError()->getErrorCode() == MegaError::API_EARGS)
                                            {
                                                OUTSTREAM << "ERROR: The link provided might be incorrect" << endl;
                                            }
                                            if (megaCmdListener->getError() && megaCmdListener->getError()->getErrorCode() == MegaError::API_EINCOMPLETE)
                                            {
                                                OUTSTREAM << "ERROR: The key is missing or wrong" << endl;
                                            }
                                        }
                                        else{
                                            if (megaCmdListener->getRequest() && megaCmdListener->getRequest()->getFlag())
                                            {
                                                LOG_err << "Key not valid " << words[1].c_str();
                                            }
                                            if (megaCmdListener->getRequest())
                                            {
                                                MegaNode *n = megaCmdListener->getRequest()->getPublicMegaNode();
                                                downloadNode(localPath, api, n);
                                                delete n;
                                            }
                                            else{
                                                LOG_err << "Empty Request at get";
                                            }
                                        }
                                        delete megaCmdListener;
                                    } else if (getLinkType(words[1]) == MegaNode::TYPE_FOLDER)
                                    {
                                        if (words.size()>2)
                                        {
                                            if (isFolder(words[2])) localPath=words[2]+"/";
                                            else
                                            {
                                                OUTSTREAM << words[2] << " is not a valid Download Folder" << endl;
                                                return;
                                            }
                                        }

                                        MegaApi* apiFolder = getFreeApiFolder();

                                        MegaCmdListener *megaCmdListener = new MegaCmdListener(apiFolder,NULL);
                                        apiFolder->loginToFolder(words[1].c_str(),megaCmdListener);
                                        megaCmdListener->wait();
                                        if (megaCmdListener->getError()->getErrorCode() == MegaError::API_OK)
                                        {
                                            MegaCmdListener *megaCmdListener2 = new MegaCmdListener(apiFolder,NULL);
                                            apiFolder->fetchNodes(megaCmdListener2);
                                            actUponFetchNodes(megaCmdListener2);
                                            delete megaCmdListener2;
                                            MegaNode *folderRootNode = apiFolder->getRootNode();
                                            MegaNode *authorizedNode = apiFolder->authorizeNode(folderRootNode);

                                            if (authorizedNode !=NULL)
                                            {
                                                //TODO: in short future: try this
                                                downloadNode(localPath, api, authorizedNode);

                                                delete authorizedNode;
                                            }
                                            else
                                            {
                                                LOG_debug << "Node couldn't be authorized: " << words[1] << ". Downloading as non-loged user";
                                                downloadNode(localPath, apiFolder, folderRootNode);
                                            }
                                            delete folderRootNode;
                                        }
                                        else{
                                            LOG_err << "Failed to login to folder: " << megaCmdListener->getError()->getErrorCode() ;
                                        }
                                        delete megaCmdListener;

        //                                MegaCmdListener *megaCmdListenerLogout = new MegaCmdListener(apiFolder,NULL);
        //                                apiFolder->logout(megaCmdListenerLogout); //todo wait for it a
        //                                megaCmdListenerLogout->wait(); //TODO: check errors
        //                                delete megaCmdListenerLogout;
                                        freeApiFolder(apiFolder);
                                    }
                                    else
                                    {
                                        OUTSTREAM << "Invalid link: " << words[1] << endl;
                                        //TODO: print usage
                                    }
                                }
                                else //remote file
                                {
                                    //wildcar
                                    if (words[1].find('*')!=string::npos || words[1].find('?')!=string::npos)// || words[1].find('/')!=string::npos)
                                    {
                                        if (words.size()>2)
                                        {
                                            if (isFolder(words[2])) localPath=words[2]+"/";
                                            else
                                            {
                                                OUTSTREAM << words[2] << " is not a valid Download Folder" << endl;
                                                return;
                                            }
                                        }

                                        vector<MegaNode *> *nodesToList = nodesbypath(words[1].c_str());
                                        if (nodesToList)
                                        {
                                            for (std::vector< MegaNode * >::iterator it = nodesToList->begin() ; it != nodesToList->end(); ++it)
                                            {
                                                MegaNode * n = *it;
                                                if (n)
                                                {
                                                    downloadNode(localPath, api, n);
                                                    delete n;
                                                }
                                            }
                                            nodesToList->clear();
                                            delete nodesToList ;
                                        }
                                    }
                                    else
                                    {
                                        MegaNode *n = nodebypath(words[1].c_str());
                                        if (n)
                                        {

                                            if (words.size()>2)
                                            {
                                                if (n->getType() == MegaNode::TYPE_FILE)
                                                {
                                                    //TODO: check permissions before download
                                                    localPath=words[2];
                                                    if (isFolder(localPath)) localPath+="/";
                                                    else
                                                    {
                                                        string containingFolder=localPath.substr(0,localPath.find_last_of("/"));
                                                        if(!isFolder(containingFolder))
                                                        {
                                                            OUTSTREAM << containingFolder << " is not a valid Download Folder" << endl;
                                                            return;
                                                        }
                                                    }
                                                }
                                                else
                                                {
                                                    if (isFolder(words[2])) localPath=words[2]+"/";
                                                    else
                                                    {
                                                        OUTSTREAM << words[2] << " is not a valid Download Folder" << endl;
                                                        return;
                                                    }
                                                }
                                            }
                                            downloadNode(localPath, api, n);
                                            delete n;
                                        }
                                        else
                                        {
                                            OUTSTREAM << "Couldn't find file" << endl;
                                        }
                                    }
                                }

//                            n = nodebypath(words[1].c_str());


//                            if (n)
//                            {
//                                if (words.size() > 2)
//                                {
//                                    // read file slice
//                                    //TODO: modify using API
////                                    client->pread(n, atol(words[2].c_str()), (words.size() > 3) ? atol(words[3].c_str()) : 0, NULL);
//                                }
//                                else
//                                {
////                                    AppFile* f;

//                                    // queue specified file...
//                                    if (n->type == FILENODE)
//                                    {
//                                        //TODO: modify using API
////                                        f = new AppFileGet(n);
////                                        f->appxfer_it = appxferq[GET].insert(appxferq[GET].end(), f);
////                                        client->startxfer(GET, f);
//                                    }
//                                    else
//                                    {
//                                        // ...or all files in the specified folder (non-recursive)
//                                        for (node_list::iterator it = n->children.begin(); it != n->children.end(); it++)
//                                        {
//                                            if ((*it)->type == FILENODE)
//                                            {
//                                                //TODO: modify using API
////                                                f = new AppFileGet(*it);
////                                                f->appxfer_it = appxferq[GET].insert(appxferq[GET].end(), f);
////                                                client->startxfer(GET, f);
//                                            }
//                                        }
//                                    }
//                                }
//                            }
//                            else
//                            {
//                                OUTSTREAM << words[1] << ": No such file or folder" << endl;
//                            }
                        }
                        else
                        {
                            OUTSTREAM << "      get remotepath [offset [length]]" << endl << "      get exportedfilelink#key [offset [length]]" << endl;
                        }

                        return;
                    }
                    else if (words[0] == "put")
                    {
                        if (words.size() > 1)
                        {
                            string targetuser;
                            string newname ="";
                            string localname;
                            string destinationFolder="";

                            MegaNode *n = NULL;

                            if (words.size() > 2)
                            {
                                destinationFolder=words[words.size()-1];
                                n = nodebypath(destinationFolder.c_str(), &targetuser, &newname);
                                if (newname != "")
                                {
                                    //TODO: create new node?
                                    n = NULL;
                                }
                            }
                            else
                            {
                                n=api->getNodeByHandle(cwd);
                            }
                            if (n)
                            {
                                if (n->getType() != MegaNode::TYPE_FILE)
                                {
                                    for (int i=1;i<max(1,(int)words.size()-1);i++)
                                    {
                                        fsAccessCMD->path2local(&words[i], &localname);
                                        if (pathExits(localname))
                                        {
                                            uploadNode(localname, api, n);
                                        }
                                        else
                                        {
                                            OUTSTREAM << "Could not find local path" << endl;
                                        }
                                    }
                                }
                                else
                                {
                                    OUTSTREAM << "Destination is not valid (expected folder or alike)" << endl;
                                }
                                delete n;
                            }
                            else
                            {
                                OUTSTREAM << "Couln't find destination folder: " << destinationFolder << endl;
                            }



//                            //TODO: modify using API
////                            if (client->loggedin() == NOTLOGGEDIN && !targetuser.size())
////                            {
////                                OUTSTREAM << "Not logged in." << endl;

////                                return;
////                            }
//                            //TODO: modify using API
////                            client->fsaccess->path2local(&words[1], &localname);

//                            //TODO: modify using API
////                            irAccess* da = client->fsaccess->newdiraccess();
////                            if (da->dopen(&localname, NULL, true))
////                            {
////                                while (da->dnext(NULL, &localname, true, &type))
////                                {
////                                    //TODO: modify using API
//////                                    client->fsaccess->local2path(&localname, &name);
////                                    OUTSTREAM << "Queueing " << name << "..." << endl;

////                                    if (type == FILENODE)
////                                    {
////                                        //TODO: modify using API
//////                                        f = new AppFilePut(&localname, target, targetuser.c_str());
//////                                        f->appxfer_it = appxferq[PUT].insert(appxferq[PUT].end(), f);
//////                                        client->startxfer(PUT, f);
//////                                        total++;
////                                    }
////                                }
////                            }
////
////                            delete da;

////                            OUTSTREAM << "Queued " << total << " file(s) for upload, " << appxferq[PUT].size()
////                                 << " file(s) in queue" << endl;
                        }
                        else
                        {
                            OUTSTREAM << "      " << getUsageStr("put") << endl;
                        }

                        return;
                    }
                    else if (words[0] == "log")
                    {
                        if (words.size()==1)
                        {
                            if (!getFlag(&clflags,"s") && ! getFlag(&clflags,"c"))
                            {
                                OUTSTREAM << "CMD log level = " << loggerCMD->getCmdLoggerLevel()<< endl;
                                OUTSTREAM << "SDK log level = " << loggerCMD->getApiLoggerLevel()<< endl;
                            }
                            else if (getFlag(&clflags,"s") )
                            {
                                OUTSTREAM << "SDK log level = " << loggerCMD->getApiLoggerLevel()<< endl;
                            }
                            else if (getFlag(&clflags,"c") )
                            {
                                OUTSTREAM << "CMD log level = " << loggerCMD->getCmdLoggerLevel()<< endl;
                            }
                        }
                        else
                        {
                            int newLogLevel = atoi (words[1].c_str());
                            newLogLevel = max(newLogLevel,(int)MegaApi::LOG_LEVEL_FATAL);
                            newLogLevel = min(newLogLevel,(int)MegaApi::LOG_LEVEL_MAX);
                            if (!getFlag(&clflags,"s") && ! getFlag(&clflags,"c"))
                            {
                                loggerCMD->setCmdLoggerLevel(newLogLevel);
                                loggerCMD->setApiLoggerLevel(newLogLevel);
                                OUTSTREAM << "CMD log level = " << loggerCMD->getCmdLoggerLevel()<< endl;
                                OUTSTREAM << "SDK log level = " << loggerCMD->getApiLoggerLevel()<< endl;
                            }
                            else if (getFlag(&clflags,"s") )
                            {
                                loggerCMD->setApiLoggerLevel(newLogLevel);
                                OUTSTREAM << "SDK log level = " << loggerCMD->getApiLoggerLevel()<< endl;
                            }
                            else if (getFlag(&clflags,"c") )
                            {
                                loggerCMD->setCmdLoggerLevel(newLogLevel);
                                OUTSTREAM << "CMD log level = " << loggerCMD->getCmdLoggerLevel()<< endl;
                            }
                        }



                        return;
                    }
                    else if (words[0] == "pwd")
                    {
                        string path;

                        nodepath(cwd, &path);

                        OUTSTREAM << path << endl;

                        return;
                    }
                    else if (words[0] == "lcd") //this only makes sense for interactive mode
                    {
                        if (words.size() > 1)
                        {
                            string localpath;

                            fsAccessCMD->path2local(&words[1], &localpath);

                            if (fsAccessCMD->chdirlocal(&localpath)) // maybe this is already checked in chdir
                            {
                                LOG_debug << "Local folder changed to: "<< localpath;
                            }
                            else
                            {
                                LOG_err << "Not a valid folder" << words[1];
                            }
                        }
                        else
                        {
                            OUTSTREAM << "      " << getUsageStr("lcd") << endl;
                        }

                        return;
                    }
                    else if (words[0] == "lpwd")
                    {
                        string cCurrentPath = getCurrentLocalPath();

                        OUTSTREAM <<  cCurrentPath << endl;
                        return;
                    }
//                    else if (words[0] == "ipc")
//                    {
//                        // incoming pending contact action
//                        handle phandle;
//                        if (words.size() == 3 && Base64::atob(words[1].c_str(), (byte*) &phandle, sizeof phandle) == sizeof phandle)
//                        {
//                            ipcactions_t action;
//                            if (words[2] == "a")
//                            {
//                                action = IPCA_ACCEPT;
//                            }
//                            else if (words[2] == "d")
//                            {
//                                action = IPCA_DENY;
//                            }
//                            else if (words[2] == "i")
//                            {
//                                action = IPCA_IGNORE;
//                            }
//                            else
//                            {
//                                OUTSTREAM << "      ipc handle a|d|i" << endl;
//                                return;
//                            }

//                            //TODO: modify using API
////                            client->updatepcr(phandle, action);
//                        }
//                        else
//                        {
//                            OUTSTREAM << "      ipc handle a|d|i" << endl;
//                        }
//                        return;
//                    }
//                    else if (words[0] == "putq")
//                    {
//                        //TODO: modify using API
////                        xferq(PUT, words.size() > 1 ? atoi(words[1].c_str()) : -1);
//                        return;
//                    }
//                    else if (words[0] == "getq")
//                    {
//                        //TODO: modify using API
////                        xferq(GET, words.size() > 1 ? atoi(words[1].c_str()) : -1);
//                        return;
//                    }
#ifdef ENABLE_SYNC
                    else if (words[0] == "sync")
                    {
                        mtxSyncMap.lock();
                        if (words.size() == 3)
                        {
                            string localpath = expanseLocalPath(words[1]);
                            MegaNode* n = nodebypath(words[2].c_str());
                            if (n)
                            {
                                if (n->getType() == MegaNode::TYPE_FILE)
                                {
                                    LOG_err << words[2] << ": Remote sync root must be folder.";
                                }
                                else if (api->getAccess(n) >= MegaShare::ACCESS_FULL)
                                {
                                    MegaCmdListener *megaCmdListener = new MegaCmdListener(api,NULL);

                                    api->syncFolder(localpath.c_str(),n,megaCmdListener);
                                    megaCmdListener->wait();//TODO: actuponsyncfolder
                                    //TODO:  api->addSyncListener();

                                    if (megaCmdListener->getError()->getErrorCode() == MegaError::API_OK )
                                    {
                                        sync_struct *thesync = new sync_struct;
                                        thesync->active = true;
                                        thesync->handle = megaCmdListener->getRequest()->getNodeHandle();
                                        thesync->localpath = string(megaCmdListener->getRequest()->getFile());
                                        thesync->fingerprint = megaCmdListener->getRequest()->getNumber();
                                        syncsmap[megaCmdListener->getRequest()->getFile()] = thesync;

                                        OUTSTREAM << "Added sync: " << megaCmdListener->getRequest()->getFile() << " to " << api->getNodePath(n);
                                    }
                                    else
                                    {
                                        LOG_err << "Sync could not be added: " << megaCmdListener->getError()->getErrorString();
                                    }

                                    delete megaCmdListener;
                                }
                                else
                                {
                                    LOG_err << words[2] << ": Syncing requires full access to path, current acces: " << api->getAccess(n);
                                }
                                delete n;
                            }
                            else
                            {
                                LOG_err << "Couldn't find remote folder: " << words[2];
                            }
                        }
                        else if (words.size() == 2)
                        {
                            int id = atoi(words[1].c_str()); //TODO: check if not a number and look by path
                            map<string,sync_struct *>::iterator itr;
                            int i =0;
                            for(itr = syncsmap.begin(); itr != syncsmap.end(); i++)
                            {
                                string key = (*itr).first;
                                sync_struct *thesync = ((sync_struct *)(*itr).second);
                                MegaNode * n = api->getNodeByHandle(thesync->handle);
                                bool erased = false;

                                if (n)
                                {
                                    if (id == i)
                                    {
                                        int nfiles=0;
                                        int nfolders=0;
                                        nfolders++; //add the share itself
                                        int *nFolderFiles = getNumFolderFiles(n,api);
                                        nfolders+=nFolderFiles[0];
                                        nfiles+=nFolderFiles[1];
                                        delete []nFolderFiles;

                                        if (getFlag(&clflags,"s"))
                                        {
                                            OUTSTREAM << "Stopping (disabling) sync "<< key << " to " << api->getNodePath(n) << endl;
                                            MegaCmdListener *megaCmdListener = new MegaCmdListener(api,NULL);
                                            if (thesync->active)
                                            {
                                                api->disableSync(n,megaCmdListener);
                                            }
                                            else
                                            {
                                                api->syncFolder(thesync->localpath.c_str(),n,megaCmdListener);
                                            }

                                            megaCmdListener->wait();//TODO: actupon...
                                            if (megaCmdListener->getError() && megaCmdListener->getError()->getErrorCode() == MegaError::API_OK)
                                            {
                                                thesync->active = !thesync->active;
                                                if (thesync->active) //syncFolder
                                                {
                                                    if (megaCmdListener->getRequest()->getNumber())
                                                        thesync->fingerprint = megaCmdListener->getRequest()->getNumber();
                                                }
                                            }
                                            delete megaCmdListener;
                                        }
                                        else if(getFlag(&clflags,"d"))
                                        {
                                            LOG_debug << "Removing sync "<< key << " to " << api->getNodePath(n);
                                            MegaCmdListener *megaCmdListener = new MegaCmdListener(api,NULL);
                                            if (thesync->active)  //if not active, removeSync will fail.)
                                            {
                                                api->removeSync(n,megaCmdListener);
                                                megaCmdListener->wait();//TODO: actupon...
                                                if (megaCmdListener->getError() && megaCmdListener->getError()->getErrorCode() == MegaError::API_OK)
                                                {
                                                    syncsmap.erase(itr++);
                                                    erased = true;
                                                    delete (thesync);
                                                    OUTSTREAM << "Removed sync "<< key << " to " << api->getNodePath(n) << endl;
                                                }
                                                else
                                                {
                                                    LOG_err << "Couldn't remove sync, errorCode = " << getErrorCodeStr(megaCmdListener->getError());
                                                }
                                            }
                                            else //if !active simply remove
                                            {
                                                //TODO: if the sdk ever provides a way to clean cache, call it
                                                syncsmap.erase(itr++);
                                                erased = true;
                                                delete (thesync);
                                            }
                                            delete megaCmdListener;
                                        }
                                        else{
                                            OUTSTREAM << i << ": " << key << " to " << api->getNodePath(n);
                                            string sstate(key);
                                            sstate = rtrim(sstate,'/');
                                            int state = api->syncPathState(&sstate);
                                            OUTSTREAM << " - " << (thesync->active?"Active":"Disabled") << " - " << getSyncStateStr(state); // << "Active"; //TODO: show inactives
                                            OUTSTREAM << ", " << api->getSize(n) << " byte(s) in ";
                                            OUTSTREAM << nfiles << " file(s) and " <<  nfolders << " folder(s)" << endl;
                                        }
                                    }
                                    delete n;
                                }
                                else
                                {
                                    LOG_err << "Node not found for sync " << key << " into handle: " << thesync->handle;
                                }
                                if (!erased) ++itr;
                            }
                        }
                        else if (words.size() == 1)
                        {
                            map<string,sync_struct *>::const_iterator itr;
                            int i =0;
                            for(itr = syncsmap.begin(); itr != syncsmap.end(); ++itr)
                            {
                                sync_struct *thesync = ((sync_struct *)(*itr).second);
                                MegaNode * n = api->getNodeByHandle(thesync->handle);

                                if (n)
                                {
                                    int nfiles=0;
                                    int nfolders=0;
                                    nfolders++; //add the share itself
                                    int *nFolderFiles = getNumFolderFiles(n,api);
                                    nfolders+=nFolderFiles[0];
                                    nfiles+=nFolderFiles[1];
                                    delete []nFolderFiles;

                                    OUTSTREAM << i++ << ": " << (*itr).first << " to " << api->getNodePath(n);
                                    string sstate((*itr).first);
                                    sstate = rtrim(sstate,'/');
                                    int state = api->syncPathState(&sstate);
                                    OUTSTREAM << " - " << ((thesync->active)?"Active":"Disabled") << " - " << getSyncStateStr(state); // << "Active"; //TODO: show inactives
                                    OUTSTREAM << ", " << api->getSize(n) << " byte(s) in ";
                                    OUTSTREAM << nfiles << " file(s) and " <<  nfolders << " folder(s)" << endl;
                                    delete n;
                                }
                                else
                                {
                                    LOG_err << "Node not found for sync " << (*itr).first << " into handle: " << thesync->handle;
                                }
                            }
                        }
                        else
                        {
                            OUTSTREAM << "      " << getUsageStr("sync") << endl;
                            mtxSyncMap.unlock();
                            return;
                        }
                        ConfigurationManager::saveSyncs(syncsmap);
                        mtxSyncMap.unlock();
                        return;
                    }
#endif
                    else if (words[0] == "login")
                    {

                        //TODO: modify using API
                        if (!api->isLoggedIn())
                        {
                            if (words.size() > 1)
                            {
                                static string pw_key;
                                if (strchr(words[1].c_str(), '@'))
                                {
                                    // full account login
                                    if (words.size() > 2)
                                    {
                                        //TODO: validate & delete
                                        MegaCmdListener *megaCmdListener = new MegaCmdListener(api,NULL);
                                        api->login(words[1].c_str(),words[2].c_str(),megaCmdListener);
                                        actUponLogin(megaCmdListener);
                                        delete megaCmdListener;

//                                        MegaCmdListener *megaCmdListener = new MegaCmdListener(api,NULL);
//                                        api->login(words[1].c_str(),words[2].c_str(),megaCmdListener);
//                                        megaCmdListener->wait(); //TODO: use a constant here
//                                        api->fetchNodes(megaCmdListener);
//                                        megaCmdListener->wait();


//                                        client->pw_key(words[2].c_str(), pwkey);
//                                        client->login(words[1].c_str(), pwkey);
//                                        OUTSTREAM << "Initiated login attempt..." << endl;
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
                                        //TODO: deal with all this
//                                        return client->app->login_result(client->folderaccess(words[1].c_str()));
                                    }
                                    else
                                    {
                                        byte session[64];

                                        if (words[1].size() < sizeof session * 4 / 3)
                                        {
                                            OUTSTREAM << "Resuming session..." << endl;
                                            MegaCmdListener *megaCmdListener = new MegaCmdListener(api,NULL);
                                            api->fastLogin(words[1].c_str(),megaCmdListener);
                                            actUponLogin(megaCmdListener);
                                            delete megaCmdListener;
                                            return;
                                        }
                                    }

                                    OUTSTREAM << "Invalid argument. Please specify a valid e-mail address, "
                                         << "a folder link containing the folder key "
                                         << "or a valid session." << endl;
                                }
                            }
                            else
                            {
                                OUTSTREAM << "      " << getUsageStr("login") << endl;
                            }
                        }
                        else
                        {
                            OUTSTREAM << "Already logged in. Please log out first." << endl;
                        }

                        return;
                    }
                    else if (words[0] == "begin")
                    {
                        if (words.size() == 1)
                        {
                            OUTSTREAM << "Creating ephemeral session..." << endl;
                            //TODO: modify using API
//                            client->createephemeral();
                        }
                        else if (words.size() == 2)
                        {
                            handle uh;
                            byte pw[SymmCipher::KEYLENGTH];

                            if (Base64::atob(words[1].c_str(), (byte*) &uh, sizeof uh) == sizeof uh && Base64::atob(
                                    words[1].c_str() + 12, pw, sizeof pw) == sizeof pw)
                            {
                                //TODO: modify using API
//                                client->resumeephemeral(uh, pw);
                            }
                            else
                            {
                                OUTSTREAM << "Malformed ephemeral session identifier." << endl;
                            }
                        }
                        else
                        {
                            OUTSTREAM << "      begin [ephemeralhandle#ephemeralpw]" << endl;
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
//                                    TreeProcListOutShares listoutshares;
//                                    Node* n;

//                                    OUTSTREAM << "Shared folders:" << endl;

//                                    //TODO: modify using API
//                                    for (unsigned i = 0; i < sizeof client->rootnodes / sizeof *client->rootnodes; i++)
//                                    {
//                                        if ((n = client->nodebyhandle(client->rootnodes[i])))
//                                        {
//                                            client->proctree(n, &listoutshares);
//                                        }
//                                    }

                                    //TODO: modify using API
//                                    for (user_map::iterator uit = client->users.begin();
//                                         uit != client->users.end(); uit++)
//                                    {
//                                        User* u = &uit->second;
//                                        Node* n;

//                                        if (u->show == VISIBLE && u->sharing.size())
//                                        {
//                                            OUTSTREAM << "From " << u->email << ":" << endl;

//                                            for (handle_set::iterator sit = u->sharing.begin();
//                                                 sit != u->sharing.end(); sit++)
//                                            {
//                                                //TODO: modify using API
////                                                if ((n = client->nodebyhandle(*sit)))
////                                                {
////                                                    OUTSTREAM << "\t" << n->displayname() << " ("
////                                                         << getAccessLevelStr(n->inshare->access) << ")" << endl;
////                                                }
//                                            }
//                                        }
//                                    }
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
                                                OUTSTREAM << "Access level must be one of r, rw or full" << endl;

                                                return;
                                            }

                                            if (words.size() > 4)
                                            {
                                                personal_representation = words[4].c_str();
                                            }
                                        }
                                        //TODO: modify using API
//                                        client->setshare(n, words[2].c_str(), a, personal_representation);
                                    }
                                }
                                else
                                {
                                    OUTSTREAM << words[1] << ": No such directory" << endl;
                                }

                                break;

                            default:
                                OUTSTREAM << "      share [remotepath [dstemail [r|rw|full] [origemail]]]" << endl;
                        }

                        return;
                    }
                    else if (words[0] == "users")
                    {
                        //TODO: modify using API
//                        for (user_map::iterator it = client->users.begin(); it != client->users.end(); it++)
//                        {
//                            if (it->second.email.size())
//                            {
//                                OUTSTREAM << "\t" << it->second.email;

//                                if (it->second.userhandle == client->me)
//                                {
//                                    OUTSTREAM << ", session user";
//                                }
//                                else if (it->second.show == VISIBLE)
//                                {
//                                    OUTSTREAM << ", visible";
//                                }
//                                else if (it->second.show == HIDDEN)
//                                {
//                                    OUTSTREAM << ", hidden";
//                                }
//                                else if (it->second.show == INACTIVE)
//                                {
//                                    OUTSTREAM << ", inactive";
//                                }
//                                else if (it->second.show == BLOCKED)
//                                {
//                                    OUTSTREAM << ", blocked";
//                                }
//                                else
//                                {
//                                    OUTSTREAM << ", unknown visibility (" << it->second.show << ")";
//                                }

//                                if (it->second.sharing.size())
//                                {
//                                    OUTSTREAM << ", sharing " << it->second.sharing.size() << " folder(s)";
//                                }

//                                if (it->second.pubk.isvalid())
//                                {
//                                    OUTSTREAM << ", public key cached";
//                                }

//                                OUTSTREAM << endl;
//                            }
//                        }

                        return;
                    }
                    else if (words[0] == "mkdir")
                    {
                        if (words.size() > 1)
                        {
                            MegaNode *currentnode=api->getNodeByHandle(cwd);
                            if (currentnode)
                            {
                                string rest = words[1];
                                while ( rest.length() )
                                {
                                    bool lastleave = false;
                                    size_t possep = rest.find_first_of("/");
                                    if (possep == string::npos )
                                    {
                                        possep = rest.length();
                                        lastleave=true;
                                    }

                                    string newfoldername=rest.substr(0,possep);
                                    if (!rest.length()) break;
                                    if (newfoldername.length())
                                    {
                                        MegaNode *existing_node = api->getChildNode(currentnode,newfoldername.c_str());
                                        if (!existing_node)
                                        {
                                            LOG_verbose << "Creating (sub)folder: " << newfoldername;
                                            MegaCmdListener *megaCmdListener = new MegaCmdListener(api,NULL);
                                            api->createFolder(newfoldername.c_str(),currentnode,megaCmdListener);
                                            actUponCreateFolder(megaCmdListener);
                                            delete megaCmdListener;
                                            MegaNode *prevcurrentNode=currentnode;
                                            currentnode = api->getChildNode(currentnode,newfoldername.c_str());
                                            delete prevcurrentNode;
                                            if (!currentnode)
                                            {
                                                LOG_err << "Couldn't get node for created subfolder: " << newfoldername;
                                                break;
                                            }
                                        }
                                        else
                                        {
                                            delete currentnode;
                                            currentnode=existing_node;
                                        }

                                        if (lastleave && existing_node)
                                        {
                                            LOG_err << "Folder already exists: " << words[1];
                                        }
                                    }

                                    //string rest = rest.substr(possep+1,rest.length()-possep-1);
                                    if (!lastleave)
                                    {
                                        rest = rest.substr(possep+1,rest.length());
                                    }
                                    else
                                    {
                                        break;
                                    }
                                }
                                delete currentnode;
                            }
                            else
                            {
                                OUTSTREAM << "      " << getUsageStr("mkdir") << endl;
                            }
                        }
                        else
                        {
                            LOG_err << "Couldn't get node for cwd handle: " << cwd;
                        }
                        return;
                    }
//                    else if (words[0] == "getfa")
//                    {
//                        if (words.size() > 1)
//                        {
//                            MegaNode* n;
//                            int cancel = words.size() > 2 && words[words.size() - 1] == "cancel";

//                            if (words.size() < 3)
//                            {
//                                //TODO: modify using API
////                                n = client->nodebyhandle(cwd);
//                            }
//                            else if (!(n = nodebypath(words[2].c_str())))
//                            {
//                                OUTSTREAM << words[2] << ": Path not found" << endl;
//                            }

//                            if (n)
//                            {
//                                int c = 0;
//                                fatype type;

//                                type = atoi(words[1].c_str());

//                                if (n->type == FILENODE)
//                                {
//                                    if (n->hasfileattribute(type))
//                                    {
//                                        //TODO: modify using API
////                                        client->getfa(n, type, cancel);
//                                        c++;
//                                    }
//                                }
//                                else
//                                {
//                                    for (node_list::iterator it = n->children.begin(); it != n->children.end(); it++)
//                                    {
//                                        if ((*it)->type == FILENODE && (*it)->hasfileattribute(type))
//                                        {
//                                            //TODO: modify using API
////                                            client->getfa(*it, type, cancel);
//                                            c++;
//                                        }
//                                    }
//                                }

//                                OUTSTREAM << (cancel ? "Canceling " : "Fetching ") << c << " file attribute(s) of type " << type << "..." << endl;
//                            }
//                        }
//                        else
//                        {
//                            OUTSTREAM << "      getfa type [path] [cancel]" << endl;
//                        }

//                        return;
//                    }
                    else if (words[0] == "getua")
                    {
                        User* u = NULL;

                        if (words.size() == 3)
                        {
                            // get other user's attribute
                            //TODO: modify using API
//                            if (!(u = client->finduser(words[2].c_str())))
//                            {
//                                OUTSTREAM << words[2] << ": Unknown user." << endl;
//                                return;
//                            }
                        }
                        else if (words.size() != 2)
                        {
                            OUTSTREAM << "      getua attrname [email]" << endl;
                            return;
                        }

                        if (!u)
                        {
                            // get logged in user's attribute
                            //TODO: modify using API
//                            if (!(u = client->finduser(client->me)))
//                            {
//                                OUTSTREAM << "Must be logged in to query own attributes." << endl;
//                                return;
//                            }
                        }

                        //TODO: modify using API
//                        client->getua(u, words[1].c_str());

                        return;
                    }
                    else if (words[0] == "putua")
                    {
                        if (words.size() == 2)
                        {
                            // delete attribute
                            //TODO: modify using API
//                            client->putua(words[1].c_str());

                            return;
                        }
                        else if (words.size() == 3)
                        {
                            if (words[2] == "del")
                            {
                                //TODO: modify using API
//                                client->putua(words[1].c_str());

                                return;
                            }
                        }
                        else if (words.size() == 4)
                        {
                            if (words[2] == "set")
                            {
                                //TODO: modify using API
//                                client->putua(words[1].c_str(), (const byte*) words[3].c_str(), words[3].size());

                                return;
                            }
                            else if (words[2] == "load")
                            {
                                string data, localpath;

                                //TODO: modify using API
//                                client->fsaccess->path2local(&words[3], &localpath);

                                if (loadfile(&localpath, &data))
                                {
                                    //TODO: modify using API
//                                    client->putua(words[1].c_str(), (const byte*) data.data(), data.size());
                                }
                                else
                                {
                                    OUTSTREAM << "Cannot read " << words[3] << endl;
                                }

                                return;
                            }
                        }

                        OUTSTREAM << "      putua attrname [del|set string|load file]" << endl;

                        return;
                    }
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
                                //TODO: modify using API
//                                if (!client->xferpaused[GET] && !client->xferpaused[PUT])
//                                {
//                                    OUTSTREAM << "Transfers not paused at the moment.";
//                                }
//                                else
//                                {
//                                    if (client->xferpaused[GET])
//                                    {
//                                        OUTSTREAM << "GETs currently paused." << endl;
//                                    }
//                                    if (client->xferpaused[PUT])
//                                    {
//                                        OUTSTREAM << "PUTs currently paused." << endl;
//                                    }
//                                }
                            }
                            else
                            {
                                OUTSTREAM << "      pause [get|put] [hard] [status]" << endl;
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
                            //TODO: modify using API
//                            client->pausexfers(GET, client->xferpaused[GET] ^= true, hardarg);
//                            if (client->xferpaused[GET])
//                            {
//                                OUTSTREAM << "GET transfers paused. Resume using the same command." << endl;
//                            }
//                            else
//                            {
//                                OUTSTREAM << "GET transfers unpaused." << endl;
//                            }
                        }

                        if (putarg)
                        {
                            //TODO: modify using API
//                            client->pausexfers(PUT, client->xferpaused[PUT] ^= true, hardarg);
//                            if (client->xferpaused[PUT])
//                            {
//                                OUTSTREAM << "PUT transfers paused. Resume using the same command." << endl;
//                            }
//                            else
//                            {
//                                OUTSTREAM << "PUT transfers unpaused." << endl;
//                            }
                        }

                        return;
                    }
                    else if (words[0] == "debug")
                    {
                        //TODO: modify using API
//                        OUTSTREAM << "Debug mode " << (client->toggledebug() ? "on" : "off") << endl;

                        return;
                    }
                    else if (words[0] == "retry")
                    {
                        //TODO: modify using API
//                        if (client->abortbackoff())
//                        {
//                            OUTSTREAM << "Retrying..." << endl;
//                        }
//                        else
//                        {
//                            OUTSTREAM << "No failed request pending." << endl;
//                        }

                        return;
                    }
                    else if (words[0] == "recon")
                    {
                        OUTSTREAM << "Closing all open network connections..." << endl;

                        //TODO: modify using API
//                        client->disconnect();

                        return;
                    }
#ifdef ENABLE_CHAT
                    else if (words[0] == "chatf")
                    {
                        //TODO: modify using API
//                        client->fetchChats();
                        return;
                    }
//                    else if (words[0] == "chatc")
//                    {
//                        unsigned wordscount = words.size();
//                        if (wordscount > 1 && ((wordscount - 2) % 2) == 0)
//                        {
//                            int group = atoi(words[1].c_str());
//                            userpriv_vector *userpriv = new userpriv_vector;

//                            unsigned numUsers = 0;
//                            while ((numUsers+1)*2 + 2 <= wordscount)
//                            {
//                                string email = words[numUsers*2 + 2];
//                                User *u = client->finduser(email.c_str(), 0);
//                                if (!u)
//                                {
//                                    OUTSTREAM << "User not found: " << email << endl;
//                                    delete userpriv;
//                                    return;
//                                }

//                                string privstr = words[numUsers*2 + 2 + 1];
//                                privilege_t priv;
//                                if (privstr ==  "ro")
//                                {
//                                    priv = PRIV_RO;
//                                }
//                                else if (privstr == "rw")
//                                {
//                                    priv = PRIV_RW;
//                                }
//                                else if (privstr == "full")
//                                {
//                                    priv = PRIV_FULL;
//                                }
//                                else if (privstr == "op")
//                                {
//                                    priv = PRIV_OPERATOR;
//                                }
//                                else
//                                {
//                                    OUTSTREAM << "Unknown privilege for " << email << endl;
//                                    delete userpriv;
//                                    return;
//                                }

//                                userpriv->push_back(userpriv_pair(u->userhandle, priv));
//                                numUsers++;
//                            }

//                            client->createChat(group, userpriv);
//                            delete userpriv;
//                            return;
//                        }
//                        else
//                        {
//                            OUTSTREAM << "      chatc group [email ro|rw|full|op]*" << endl;
//                            return;
//                        }
//                    }
//                    else if (words[0] == "chati")
//                    {
//                        if (words.size() == 4)
//                        {
//                            handle chatid;
//                            Base64::atob(words[1].c_str(), (byte*) &chatid, sizeof chatid);

//                            string email = words[2];
//                            User *u = client->finduser(email.c_str(), 0);
//                            if (!u)
//                            {
//                                OUTSTREAM << "User not found: " << email << endl;
//                                return;
//                            }

//                            string privstr = words[3];
//                            privilege_t priv;
//                            if (privstr ==  "ro")
//                            {
//                                priv = PRIV_RO;
//                            }
//                            else if (privstr == "rw")
//                            {
//                                priv = PRIV_RW;
//                            }
//                            else if (privstr == "full")
//                            {
//                                priv = PRIV_FULL;
//                            }
//                            else if (privstr == "op")
//                            {
//                                priv = PRIV_OPERATOR;
//                            }
//                            else
//                            {
//                                OUTSTREAM << "Unknown privilege for " << email << endl;
//                                return;
//                            }

//                            client->inviteToChat(chatid, u->uid.c_str(), priv);
//                            return;
//                        }
//                        else
//                        {
//                            OUTSTREAM << "      chati chatid email ro|rw|full|op" << endl;
//                            return;

//                        }
//                    }
//                    else if (words[0] == "chatr")
//                    {
//                        if (words.size() > 1)
//                        {
//                            handle chatid;
//                            Base64::atob(words[1].c_str(), (byte*) &chatid, sizeof chatid);

//                            if (words.size() == 2)
//                            {
//                                client->removeFromChat(chatid);
//                            }
//                            else if (words.size() == 3)
//                            {
//                                string email = words[2];
//                                User *u = client->finduser(email.c_str(), 0);
//                                if (!u)
//                                {
//                                    OUTSTREAM << "User not found: " << email << endl;
//                                    return;
//                                }

//                                client->removeFromChat(chatid, u->uid.c_str());
//                                return;
//                            }
//                            else
//                            {
//                                OUTSTREAM << "      chatr chatid [email]" << endl;
//                                return;
//                            }
//                        }
//                        else
//                        {
//                            OUTSTREAM << "      chatr chatid [email]" << endl;
//                            return;
//                        }

//                    }
//                    else if (words[0] == "chatu")
//                    {
//                        if (words.size() == 2)
//                        {
//                            handle chatid;
//                            Base64::atob(words[1].c_str(), (byte*) &chatid, sizeof chatid);

//                            client->getUrlChat(chatid);
//                            return;
//                        }
//                        else
//                        {
//                            OUTSTREAM << "      chatu chatid" << endl;
//                            return;
//                        }
//                    }
#endif
                    if (words[0] == "passwd")
                    {
                        //TODO: modify using API
//                        if (client->loggedin() != NOTLOGGEDIN)
//                        {
//                            setprompt(OLDPASSWORD);
//                        }
//                        else
//                        {
//                            OUTSTREAM << "Not logged in." << endl;
//                        }

                        return;
                    }
                    else if (words[0] == "putbps")
                    {
                        if (words.size() > 1)
                        {
                            if (words[1] == "auto")
                            {
                                //TODO: modify using API
//                                client->putmbpscap = -1;
                            }
                            else if (words[1] == "none")
                            {
                                //TODO: modify using API
//                                client->putmbpscap = 0;
                            }
                            else
                            {
                                int t = atoi(words[1].c_str());

                                if (t > 0)
                                {
                                    //TODO: modify using API
//                                    client->putmbpscap = t;
                                }
                                else
                                {
                                    OUTSTREAM << "      putbps [limit|auto|none]" << endl;
                                    return;
                                }
                            }
                        }

                        OUTSTREAM << "Upload speed limit set to ";

                        //TODO: modify using API
//                        if (client->putmbpscap < 0)
//                        {
//                            OUTSTREAM << "AUTO (approx. 90% of your available bandwidth)" << endl;
//                        }
//                        else if (!client->putmbpscap)
//                        {
//                            OUTSTREAM << "NONE" << endl;
//                        }
//                        else
//                        {
//                            OUTSTREAM << client->putmbpscap << " byte(s)/second" << endl;
//                        }

                        return;
                    }
                    else if (words[0] == "invite")
                    {
                        //TODO: modify using API
//                        if (client->finduser(client->me)->email.compare(words[1]))
//                        {
//                            int del = words.size() == 3 && words[2] == "del";
//                            int rmd = words.size() == 3 && words[2] == "rmd";
//                            if (words.size() == 2 || words.size() == 3)
//                            {
//                                if (del || rmd)
//                                {
//                                    client->setpcr(words[1].c_str(), del ? OPCA_DELETE : OPCA_REMIND);
//                                }
//                                else
//                                {
//                                    // Original email is not required, but can be used if this account has multiple email addresses associated,
//                                    // to have the invite come from a specific email
//                                    client->setpcr(words[1].c_str(), OPCA_ADD, "Invite from MEGAcli", words.size() == 3 ? words[2].c_str() : NULL);
//                                }
//                            }
//                            else
//                            {
//                                OUTSTREAM << "      invite dstemail [origemail|del|rmd]" << endl;
//                            }
//                        }
//                        else
//                        {
//                            OUTSTREAM << "Cannot send invitation to your own user" << endl;
//                        }

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
                            //TODO: modify using API
//                            client->querysignuplink(c, len);
                            delete[] c;
                        }
                        else if (words.size() == 3)
                        {
                            //TODO: modify using API
//                            switch (client->loggedin())
//                            {
//                                case FULLACCOUNT:
//                                    OUTSTREAM << "Already logged in." << endl;
//                                    break;

//                                case CONFIRMEDACCOUNT:
//                                    OUTSTREAM << "Current account already confirmed." << endl;
//                                    break;

//                                case EPHEMERALACCOUNT:
//                                    if (words[1].find('@') + 1 && words[1].find('.') + 1)
//                                    {
//                                        signupemail = words[1];
//                                        signupname = words[2];

//                                        OUTSTREAM << endl;
//                                        setprompt(NEWPASSWORD);
//                                    }
//                                    else
//                                    {
//                                        OUTSTREAM << "Please enter a valid e-mail address." << endl;
//                                    }
//                                    break;

//                                case NOTLOGGEDIN:
//                                    OUTSTREAM << "Please use the begin command to commence or resume the ephemeral session to be upgraded." << endl;
//                            }
                        }

                        return;
                    }
                    else if (words[0] == "whoami")
                    {
                        MegaUser *u = api->getMyUser();
                        if (u)
                        {
                            OUTSTREAM << "Account e-mail: " << u->getEmail() << endl;
                            MegaCmdListener *megaCmdListener = new MegaCmdListener(api,NULL);
                            api->getExtendedAccountDetails(true,true,true,megaCmdListener);//TODO: continue this.
                            actUponGetExtendedAccountDetails(megaCmdListener);
                            delete megaCmdListener;
                            delete u;
                        }
                        else
                        {
                            OUTSTREAM << "Not logged in." << endl;
                        }


                        //TODO: modify using API
//                        if (client->loggedin() == NOTLOGGEDIN)
//                        {
//                            OUTSTREAM << "Not logged in." << endl;
//                        }
//                        else
//                        {
//                            User* u;

//                            if ((u = client->finduser(client->me)))
//                            {
//                                OUTSTREAM << "Account e-mail: " << u->email << endl;
//                            }

//                            OUTSTREAM << "Retrieving account status..." << endl;

//                            client->getaccountdetails(&account, true, true, true, true, true, true);
//                        }

                        return;
                    }
//                    else if (words[0] == "export")
//                    {
//                        if (words.size() > 1)
//                        {
//                            Node* n;
//                            int del = 0;
//                            int ets = 0;

//                            if ((n = nodebypath(words[1].c_str())))
//                            {
//                                if (words.size() > 2)
//                                {
//                                    del = (words[2] == "del");
//                                    if (!del)
//                                    {
//                                        ets = atol(words[2].c_str());
//                                    }
//                                }

//                                OUTSTREAM << "Exporting..." << endl;

//                                error e;
//                                //TODO: modify using API
////                                if ((e = client->exportnode(n, del, ets)))
////                                {
////                                    OUTSTREAM << words[1] << ": Export rejected (" << errorstring(e) << ")" << endl;
////                                }
//                            }
//                            else
//                            {
//                                OUTSTREAM << words[1] << ": Not found" << endl;
//                            }
//                        }
//                        else
//                        {
//                            OUTSTREAM << "      export remotepath [expireTime|del]" << endl;
//                        }

//                        return;
//                    }
                    else if (words[0] == "import")
                    {
                        if (words.size() > 1)
                        {
                            //TODO: modify using API
//                            if (client->openfilelink(words[1].c_str(), 1) == API_OK)
//                            {
//                                OUTSTREAM << "Opening link..." << endl;
//                            }
//                            else
//                            {
//                                OUTSTREAM << "Malformed link. Format: Exported URL or fileid#filekey" << endl;
//                            }
                        }
                        else
                        {
                            OUTSTREAM << "      import exportedfilelink#key" << endl;
                        }

                        return;
                    }
                    else if (words[0] == "reload")
                    {
                        OUTSTREAM << "Reloading account..." << endl;
                        MegaCmdListener *megaCmdListener = new MegaCmdListener(api,NULL);
                        api->fetchNodes(megaCmdListener);
                        actUponFetchNodes(megaCmdListener);
                        delete megaCmdListener;
                        return;
                    }
                    else if (words[0] == "logout")
                    {
                        OUTSTREAM << "Logging off..." << endl;
                        MegaCmdListener *megaCmdListener = new MegaCmdListener(api,NULL);
                        api->logout(megaCmdListener);
                        actUponLogout(megaCmdListener);
                        delete megaCmdListener;
                        return;
                    }
#ifdef ENABLE_CHAT
//                    else if (words[0] == "chatga")
//                    {
//                        if (words.size() == 4)
//                        {
//                            handle chatid;
//                            Base64::atob(words[1].c_str(), (byte*) &chatid, sizeof chatid);

//                            handle nodehandle;
//                            Base64::atob(words[2].c_str(), (byte*) &nodehandle, sizeof nodehandle);

//                            const char *uid = words[3].c_str();

//                            client->grantAccessInChat(chatid, nodehandle, uid);
//                            return;
//                        }
//                        else
//                        {
//                            OUTSTREAM << "       chatga chatid nodehandle uid" << endl;
//                            return;
//                        }

//                    }
//                    else if (words[0] == "chatra")
//                    {
//                        if (words.size() == 4)
//                        {
//                            handle chatid;
//                            Base64::atob(words[1].c_str(), (byte*) &chatid, sizeof chatid);

//                            handle nodehandle;
//                            Base64::atob(words[2].c_str(), (byte*) &nodehandle, sizeof nodehandle);

//                            const char *uid = words[3].c_str();

//                            client->removeAccessInChat(chatid, nodehandle, uid);
//                            return;
//                        }
//                        else
//                        {
//                            OUTSTREAM << "       chatra chatid nodehandle uid" << endl;
//                            return;
//                        }
//                    }
#endif

                    else if (words[0] == "confirm")
                    {
                        if (signupemail.size() && signupcode.size())
                        {
                            OUTSTREAM << "Please type " << signupemail << "'s password to confirm the signup." << endl;
                            setprompt(LOGINPASSWORD);
                        }
                        else
                        {
                            OUTSTREAM << "No signup confirmation pending." << endl;
                        }

                        return;
                    }
                    else if (words[0] == "session")
                    {
                        char * dumpSession = api->dumpSession();
                        if (dumpSession)
                        {
                            OUTSTREAM << "Your (secret) session is: " << dumpSession << endl;
                            delete []dumpSession;
                        }
                        else
                        {
                            OUTSTREAM << "Not logged in." << endl;
                        }
                        return;
                    }
                    else if (words[0] == "symlink")
                    {
                        //TODO: modify using API
//                        if (client->followsymlinks ^= true)
//                        {
//                            OUTSTREAM << "Now following symlinks. Please ensure that sync does not see any filesystem item twice!" << endl;
//                        }
//                        else
//                        {
//                            OUTSTREAM << "No longer following symlinks." << endl;
//                        }

                        return;
                    }
                    else if (words[0] == "version")
                    {
                        OUTSTREAM << "MEGA SDK version: " << MEGA_MAJOR_VERSION << "." << MEGA_MINOR_VERSION << "." << MEGA_MICRO_VERSION << endl;

                        OUTSTREAM << "Features enabled:" << endl;

#ifdef USE_CRYPTOPP
                        OUTSTREAM << "* CryptoPP" << endl;
#endif

#ifdef USE_SQLITE
                        OUTSTREAM << "* SQLite" << endl;
#endif

#ifdef USE_BDB
                        OUTSTREAM << "* Berkeley DB" << endl;
#endif

#ifdef USE_INOTIFY
                        OUTSTREAM << "* inotify" << endl;
#endif

#ifdef HAVE_FDOPENDIR
                        OUTSTREAM << "* fdopendir" << endl;
#endif

#ifdef HAVE_SENDFILE
                        OUTSTREAM << "* sendfile" << endl;
#endif

#ifdef _LARGE_FILES
                        OUTSTREAM << "* _LARGE_FILES" << endl;
#endif

#ifdef USE_FREEIMAGE
                        OUTSTREAM << "* FreeImage" << endl;
#endif

#ifdef ENABLE_SYNC
                        OUTSTREAM << "* sync subsystem" << endl;
#endif


                        cwd = UNDEF;

                        return;
                    }
                    else if (words[0] == "showpcr")
                    {
                        string outgoing = "";
                        string incoming = "";
                        //TODO: modify using API
//                        for (handlepcr_map::iterator it = client->pcrindex.begin(); it != client->pcrindex.end(); it++)
//                        {
//                            if (it->second->isoutgoing)
//                            {
//                                ostringstream os;
//                                os << setw(34) << it->second->targetemail;

//                                char buffer[12];
//                                int size = Base64::btoa((byte*)&(it->second->id), sizeof(it->second->id), buffer);
//                                os << "\t(id: ";
//                                os << buffer;

//                                os << ", ts: ";

//                                os << it->second->ts;

//                                outgoing.append(os.str());
//                                outgoing.append(")\n");
//                            }
//                            else
//                            {
//                                ostringstream os;
//                                os << setw(34) << it->second->originatoremail;

//                                char buffer[12];
//                                int size = Base64::btoa((byte*)&(it->second->id), sizeof(it->second->id), buffer);
//                                os << "\t(id: ";
//                                os << buffer;

//                                os << ", ts: ";

//                                os << it->second->ts;

//                                incoming.append(os.str());
//                                incoming.append(")\n");
//                            }
//                        }
//                        OUTSTREAM << "Incoming PCRs:" << endl << incoming << endl;
//                        OUTSTREAM << "Outgoing PCRs:" << endl << outgoing << endl;
//                        return;
                    }

                    else if (words[0] == "killsession")
                    {
                        if (words.size() == 2)
                        {
                            if (words[1] == "all")
                            {
                                // Kill all sessions (except current)
                                //TODO: modify using API
//                                client->killallsessions();
                            }
                            else
                            {
                                handle sessionid;
                                if (Base64::atob(words[1].c_str(), (byte*) &sessionid, sizeof sessionid) == sizeof sessionid)
                                {
                                    //TODO: modify using API
//                                    client->killsession(sessionid);
                                }
                                else
                                {
                                    OUTSTREAM << "invalid session id provided" << endl;
                                }
                            }
                        }
                        else
                        {
                            OUTSTREAM << "      killsession [all|sessionid] " << endl;
                        }
                        return;
                    }
                    else if (words[0] == "locallogout")
                    {
                        OUTSTREAM << "Logging off locally..." << endl;

                        cwd = UNDEF;
                        //TODO: modify using API
//                        client->locallogout();

                        return;
                    }
                    break;
            }

            OUTSTREAM << "?Invalid command" << endl;

}
/*
// callback for non-EAGAIN request-level errors
// in most cases, retrying is futile, so the application exits
// this can occur e.g. with syntactically malformed requests (due to a bug), an invalid application key
void DemoApp::request_error(error e)
{
    if ((e == API_ESID) || (e == API_ENOENT))   // Invalid session or Invalid folder handle
    {
        OUTSTREAM << "Invalid or expired session, logging OUTSTREAM..." << endl;
        //TODO: modify using API
//        client->locallogout();
        return;
    }

    OUTSTREAM << "FATAL: Request failed (" << errorstring(e) << "), exiting" << endl;

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
        OUTSTREAM << "Login failed: " << errorstring(e) << endl;
    }
    else
    {
        OUTSTREAM << "Login successful, retrieving account..." << endl;
        //TODO: modify using API
//        client->fetchnodes();
    }
}

// ephemeral session result
void DemoApp::ephemeral_result(error e)
{
    if (e)
    {
        OUTSTREAM << "Ephemeral session error (" << errorstring(e) << ")" << endl;
    }
}

// signup link send request result
void DemoApp::sendsignuplink_result(error e)
{
    if (e)
    {
        OUTSTREAM << "Unable to send signup link (" << errorstring(e) << ")" << endl;
    }
    else
    {
        OUTSTREAM << "Thank you. Please check your e-mail and enter the command signup followed by the confirmation link." << endl;
    }
}

// signup link query result
void DemoApp::querysignuplink_result(handle uh, const char* email, const char* name, const byte* pwc, const byte* kc,
                                     const byte* c, size_t len)
{
    OUTSTREAM << "Ready to confirm user account " << email << " (" << name << ") - enter confirm to execute." << endl;

    signupemail = email;
    signupcode.assign((char*) c, len);
    memcpy(signuppwchallenge, pwc, sizeof signuppwchallenge);
    memcpy(signupencryptedmasterkey, pwc, sizeof signupencryptedmasterkey);
}

// signup link query failed
void DemoApp::querysignuplink_result(error e)
{
    OUTSTREAM << "Signuplink confirmation failed (" << errorstring(e) << ")" << endl;
}

// signup link (account e-mail) confirmation result
void DemoApp::confirmsignuplink_result(error e)
{
    if (e)
    {
        OUTSTREAM << "Signuplink confirmation failed (" << errorstring(e) << ")" << endl;
    }
    else
    {
        OUTSTREAM << "Signup confirmed, logging in..." << endl;
        client->login(signupemail.c_str(), pwkey);
    }
}

// asymmetric keypair configuration result
void DemoApp::setkeypair_result(error e)
{
    if (e)
    {
        OUTSTREAM << "RSA keypair setup failed (" << errorstring(e) << ")" << endl;
    }
    else
    {
        OUTSTREAM << "RSA keypair added. Account setup complete." << endl;
    }
}

void DemoApp::ephemeral_result(handle uh, const byte* pw)
{
    char buf[SymmCipher::KEYLENGTH * 4 / 3 + 3];

    OUTSTREAM << "Ephemeral session established, session ID: ";
    Base64::btoa((byte*) &uh, sizeof uh, buf);
    OUTSTREAM << buf << "#";
    Base64::btoa(pw, SymmCipher::KEYLENGTH, buf);
    OUTSTREAM << buf << endl;

    client->fetchnodes();
}

// password change result
void DemoApp::changepw_result(error e)
{
    if (e)
    {
        OUTSTREAM << "Password update failed: " << errorstring(e) << endl;
    }
    else
    {
        OUTSTREAM << "Password updated." << endl;
    }
}

// node export failed
void DemoApp::exportnode_result(error e)
{
    if (e)
    {
        OUTSTREAM << "Export failed: " << errorstring(e) << endl;
    }
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

        OUTSTREAM << "Exported " << path << ": ";

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
            OUTSTREAM << "No key available for exported folder" << endl;
            return;
        }

        OUTSTREAM << "https://mega.co.nz/#" << (n->type ? "F" : "") << "!" << node << "!" << key << endl;
    }
    else
    {
        OUTSTREAM << "Exported node no longer available" << endl;
    }
}

// the requested link could not be opened
void DemoApp::openfilelink_result(error e)
{
    if (e)
    {
        OUTSTREAM << "Failed to open link: " << errorstring(e) << endl;
    }
}

// the requested link was opened successfully - import to cwd
void DemoApp::openfilelink_result(handle ph, const byte* key, m_off_t size,
                                  string* a, string* fa, int)
{
    Node* n;

    if (!key)
    {
        OUTSTREAM << "File is valid, but no key was provided." << endl;
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
    if(!buf)
    {
        OUTSTREAM << "The file won't be imported, the provided key is invalid." << endl;
    }
    else if (client->loggedin() != NOTLOGGEDIN && (n = client->nodebyhandle(cwd)))
    {
        NewNode* newnode = new NewNode[1];

        // set up new node as folder node
        newnode->source = NEW_PUBLIC;
        newnode->type = FILENODE;
        newnode->nodehandle = ph;
        newnode->parenthandle = UNDEF;

        newnode->nodekey.assign((char*)key, FILENODEKEYLENGTH);

        newnode->attrstring = new string(*a);

        client->putnodes(n->nodehandle, newnode, 1);
    }
    else
    {
        OUTSTREAM << "Need to be logged in to import file links." << endl;
    }

    delete [] buf;
}

void DemoApp::checkfile_result(handle h, error e)
{
    OUTSTREAM << "Link check failed: " << errorstring(e) << endl;
}

void DemoApp::checkfile_result(handle h, error e, byte* filekey, m_off_t size, m_time_t ts, m_time_t tm, string* filename,
                               string* fingerprint, string* fileattrstring)
{
    OUTSTREAM << "Name: " << *filename << ", size: " << size;

    if (fingerprint->size())
    {
        OUTSTREAM << ", fingerprint available";
    }

    if (fileattrstring->size())
    {
        OUTSTREAM << ", has attributes";
    }

    OUTSTREAM << endl;

    if (e)
    {
        OUTSTREAM << "Not available: " << errorstring(e) << endl;
    }
    else
    {
        OUTSTREAM << "Initiating download..." << endl;
        //TODO: modify using API
//        AppFileGet* f = new AppFileGet(NULL, h, filekey, size, tm, filename, fingerprint);
//        f->appxfer_it = appxferq[GET].insert(appxferq[GET].end(), f);
//        client->startxfer(GET, f);
    }
}

bool DemoApp::pread_data(byte* data, m_off_t len, m_off_t pos, void* appdata)
{
    OUTSTREAM << "Received " << len << " partial read byte(s) at position " << pos << ": ";
    fwrite(data, 1, len, stdout);
    OUTSTREAM << endl;

    return true;
}

dstime DemoApp::pread_failure(error e, int retry, void* appdata)
{
    if (retry < 5)
    {
        OUTSTREAM << "Retrying read (" << errorstring(e) << ", attempt #" << retry << ")" << endl;
        return (dstime)(retry*10);
    }
    else
    {
        OUTSTREAM << "Too many failures (" << errorstring(e) << "), giving up" << endl;
        return ~(dstime)0;
    }
}

// reload needed
void DemoApp::reload(const char* reason)
{
    OUTSTREAM << "Reload suggested (" << reason << ") - use 'reload' to trigger" << endl;
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

// display account details/history
void DemoApp::account_details(AccountDetails* ad, bool storage, bool transfer, bool pro, bool purchases,
                              bool transactions, bool sessions)
{
    char timebuf[32], timebuf2[32];

    if (storage)
    {
        OUTSTREAM << "\tAvailable storage: " << ad->storage_max << " byte(s)" << endl;

        for (unsigned i = 0; i < sizeof rootnodenames/sizeof *rootnodenames; i++)
        {
            NodeStorage* ns = &ad->storage[client->rootnodes[i]];

            OUTSTREAM << "\t\tIn " << rootnodenames[i] << ": " << ns->bytes << " byte(s) in " << ns->files << " file(s) and " << ns->folders << " folder(s)" << endl;
        }
    }

    if (transfer)
    {
        if (ad->transfer_max)
        {
            OUTSTREAM << "\tTransfer in progress: " << ad->transfer_own_reserved << "/" << ad->transfer_srv_reserved << endl;
            OUTSTREAM << "\tTransfer completed: " << ad->transfer_own_used << "/" << ad->transfer_srv_used << " of "
                 << ad->transfer_max << " ("
                 << (100 * (ad->transfer_own_used + ad->transfer_srv_used) / ad->transfer_max) << "%)" << endl;
            OUTSTREAM << "\tServing bandwidth ratio: " << ad->srv_ratio << "%" << endl;
        }

        if (ad->transfer_hist_starttime)
        {
            time_t t = time(NULL) - ad->transfer_hist_starttime;

            OUTSTREAM << "\tTransfer history:\n";

            for (unsigned i = 0; i < ad->transfer_hist.size(); i++)
            {
                t -= ad->transfer_hist_interval;
                OUTSTREAM << "\t\t" << t;
                if (t < ad->transfer_hist_interval)
                {
                    OUTSTREAM << " second(s) ago until now: ";
                }
                else
                {
                    OUTSTREAM << "-" << t - ad->transfer_hist_interval << " second(s) ago: ";
                }
                OUTSTREAM << ad->transfer_hist[i] << " byte(s)" << endl;
            }
        }

        if (ad->transfer_limit)
        {
            OUTSTREAM << "Per-IP transfer limit: " << ad->transfer_limit << endl;
        }
    }

    if (pro)
    {
        OUTSTREAM << "\tPro level: " << ad->pro_level << endl;
        OUTSTREAM << "\tSubscription type: " << ad->subscription_type << endl;
        OUTSTREAM << "\tAccount balance:" << endl;

        for (vector<AccountBalance>::iterator it = ad->balances.begin(); it != ad->balances.end(); it++)
        {
            printf("\tBalance: %.3s %.02f\n", it->currency, it->amount);
        }
    }

    if (purchases)
    {
        OUTSTREAM << "Purchase history:" << endl;

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
        OUTSTREAM << "Transaction history:" << endl;

        for (vector<AccountTransaction>::iterator it = ad->transactions.begin(); it != ad->transactions.end(); it++)
        {
            time_t ts = it->timestamp;
            strftime(timebuf, sizeof timebuf, "%c", localtime(&ts));
            printf("\tID: %.11s Time: %s Delta: %.3s %.02f\n", it->handle, timebuf, it->currency, it->delta);
        }
    }

    if (sessions)
    {
        OUTSTREAM << "Currently Active Sessions:" << endl;
        for (vector<AccountSession>::iterator it = ad->sessions.begin(); it != ad->sessions.end(); it++)
        {
            if (it->alive)
            {
                time_t ts = it->timestamp;
                strftime(timebuf, sizeof timebuf, "%c", localtime(&ts));
                ts = it->mru;
                strftime(timebuf2, sizeof timebuf, "%c", localtime(&ts));

                char id[12];
                Base64::btoa((byte*)&(it->id), sizeof(it->id), id);

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
            OUTSTREAM << endl << "Full Session history:" << endl;

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
        OUTSTREAM << "Account details retrieval failed (" << errorstring(e) << ")" << endl;
    }
}

// account details could not be retrieved
void DemoApp::sessions_killed(handle sessionid, error e)
{
    if (e)
    {
        OUTSTREAM << "Session killing failed (" << errorstring(e) << ")" << endl;
        return;
    }

    if (sessionid == UNDEF)
    {
        OUTSTREAM << "All sessions except current have been killed" << endl;
    }
    else
    {
        char id[12];
        int size = Base64::btoa((byte*)&(sessionid), sizeof(sessionid), id);
        OUTSTREAM << "Session with id " << id << " has been killed" << endl;
    }
}


// user attribute update notification
void DemoApp::userattr_update(User* u, int priv, const char* n)
{
    OUTSTREAM << "Notification: User " << u->email << " -" << (priv ? " private" : "") << " attribute "
          << n << " added or updated" << endl;
}
*/


void * doProcessLine(void *pointer)
{
    petition_info_t *inf = (petition_info_t *) pointer;

    std::ostringstream   s;
    setCurrentThreadOutStream(&s);
    setCurrentThreadLogLevel(MegaApi::LOG_LEVEL_ERROR);


    LOG_verbose << " Processing " << inf->line << " in thread: " << getCurrentThread()
              << " socket output: " <<  inf->outSocket ;

    process_line(inf->line);

    LOG_verbose << " Procesed " << inf->line << " in thread: " << getCurrentThread()
              << " socket output: " <<  inf->outSocket ;

    LOG_verbose << "Output to write in socket " <<inf->outSocket << ": <<" << s.str() << ">>";

    cm->returnAndClosePetition(inf,&s);

    return NULL;
}

// main loop
void megacmd()
{
    char *saved_line = NULL;
    int saved_point = 0;

    rl_save_prompt();


//    int readline_fd = fileno(rl_instream);
    int readline_fd = STDIN_FILENO;//stdin



    for (;;)
    {
        if (prompt == COMMAND)
        {
            // display put/get transfer speed in the prompt
            //TODO: modify using API
//            if (client->tslots.size() || responseprogress >= 0)
//            {
//                unsigned xferrate[2] = { 0 };
//                Waiter::bumpds();

//                for (transferslot_list::iterator it = client->tslots.begin(); it != client->tslots.end(); it++)
//                {
//                    if ((*it)->fa)
//                    {
//                        xferrate[(*it)->transfer->type]
//                            += (*it)->progressreported * 10 / (1024 * (Waiter::ds - (*it)->starttime + 1));
//                    }
//                }

//                strcpy(dynamicprompt, "MEGA");

//                if (xferrate[GET] || xferrate[PUT] || responseprogress >= 0)
//                {
//                    strcpy(dynamicprompt + 4, " (");

//                    if (xferrate[GET])
//                    {
//                        sprintf(dynamicprompt + 6, "In: %u KB/s", xferrate[GET]);

//                        if (xferrate[PUT])
//                        {
//                            strcat(dynamicprompt + 9, "/");
//                        }
//                    }

//                    if (xferrate[PUT])
//                    {
//                        sprintf(strchr(dynamicprompt, 0), "Out: %u KB/s", xferrate[PUT]);
//                    }

//                    if (responseprogress >= 0)
//                    {
//                        sprintf(strchr(dynamicprompt, 0), "%d%%", responseprogress);
//                    }

//                    strcat(dynamicprompt + 6, ")");
//                }

//                strcat(dynamicprompt + 4, "> ");
//            }
//            else
//            {
//                *dynamicprompt = 0;
//            }

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
//        int rc = -1;

        // command editing loop - exits when a line is submitted or the engine requires the CPU
        for (;;)
        {
            //TODO: modify using API
//            int w = client->wait();

//            if (w & Waiter::HAVESTDIN)
            if (Waiter::HAVESTDIN)
            {
                if (prompt == COMMAND)
                {
                    cm->waitForPetitionOrReadlineInput(readline_fd);

                    if (cm->receivedReadlineInput(readline_fd)) {
                        rl_callback_read_char();
                    }
                    else if (cm->receivedPetition())
                    {
                        LOG_verbose << "Client connected ";
                        //TODO: limit max number of simultaneous connection (otherwise will fail due to too many files opened)

                        petition_info_t *inf = cm->getPetition();

                        LOG_verbose << "petition registered: " << inf->line;

                        delete_finished_threads();

                        //append new one
                        MegaThread * petitionThread = new MegaThread();
                        petitionThreads.push_back(petitionThread);

                        LOG_debug << "starting processing: " << inf->line;
                        petitionThread->start(doProcessLine, (void *)inf);
                    }
                }
                else
                {
                    console->readpwchar(pw_buf, sizeof pw_buf, &pw_buf_pos, &line);
                }
            }

//            if (w & Waiter::NEEDEXEC || line)
            if (Waiter::NEEDEXEC || line)
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
        //TODO: modify using API
//        client->exec();
    }
}

class NullBuffer : public std::streambuf
{
public:
  int overflow(int c) { return c; }
};
int main()
{
    NullBuffer null_buffer;
    std::ostream null_stream(&null_buffer);
    SimpleLogger::setAllOutputs(&null_stream);


    fsAccessCMD = new MegaFileSystemAccess();

//    SimpleLogger::setAllOutputs(&cout);
//    SimpleLogger::setAllOutputs(new NulOStream());

    mtxSyncMap.init(false);


    // instantiate app components: the callback processor (DemoApp),
    // the HTTP I/O engine (WinHttpIO) and the MegaClient itself
    //TODO: modify using API
//    client = new MegaClient(new DemoApp, new CONSOLE_WAIT_CLASS,
//                            new HTTPIO_CLASS, new FSACCESS_CLASS,
//#ifdef DBACCESS_CLASS
//                            new DBACCESS_CLASS,
//#else
//                            NULL,
//#endif
//#ifdef GFX_CLASS
//                            new GFX_CLASS,
//#else
//                            NULL,
//#endif
//                            "SDKSAMPLE",
//                            "megacli/" TOSTRING(MEGA_MAJOR_VERSION)
//                            "." TOSTRING(MEGA_MINOR_VERSION)
//                            "." TOSTRING(MEGA_MICRO_VERSION));



    api=new MegaApi("BdARkQSQ",(const char*)NULL, "MegaCMD User Agent"); // TODO: store user agent somewhere, and use path to cache!
    for (int i=0;i<10;i++)
    {
        MegaApi *apiFolder=new MegaApi("BdARkQSQ",(const char*)NULL, "MegaCMD User Agent"); // TODO: store user agent somewhere, and use path to cache!
        apiFolders.push(apiFolder);
        apiFolder->setLoggerObject(loggerCMD);
        apiFolder->setLogLevel(MegaApi::LOG_LEVEL_MAX);
        semaphoreapiFolders.release();
    }
    mutexapiFolders.init(false);

    loggerCMD = new MegaCMDLogger(&cout); //TODO: never deleted

//    loggerCMD->setApiLoggerLevel(MegaApi::LOG_LEVEL_ERROR);
//    loggerCMD->setApiLoggerLevel(MegaApi::LOG_LEVEL_MAX);
    loggerCMD->setApiLoggerLevel(MegaApi::LOG_LEVEL_DEBUG);

//    loggerCMD->setCmdLoggerLevel(MegaApi::LOG_LEVEL_INFO);
    loggerCMD->setCmdLoggerLevel(MegaApi::LOG_LEVEL_DEBUG);
//    loggerCMD->setCmdLoggerLevel(MegaApi::LOG_LEVEL_ERROR);

    api->setLoggerObject(loggerCMD);
    api->setLogLevel(MegaApi::LOG_LEVEL_MAX);



    megaCmdGlobalListener =  new MegaCmdGlobalListener();

    api->addGlobalListener(megaCmdGlobalListener);

//    SimpleLogger::setLogLevel(-1) ;//Do not log via simplelogger
//      SimpleLogger::setLogLevel(logDebug);
//    SimpleLogger::setLogLevel(logError);
    //    SimpleLogger::setLogLevel(logFatal);
        SimpleLogger::setLogLevel(logMax); // log level checking is done by loggerCMD

    console = new CONSOLE_CLASS;

    cm = new ComunicationsManager();

#ifdef __linux__
    // prevent CTRL+C exit
    signal(SIGINT, sigint_handler);


#endif

    atexit(finalize);

    rl_callback_handler_install(NULL,NULL); //this initializes readline somehow,
            // so that we can use rl_message or rl_resize_terminal safely before ever
            // prompting anything.

    ConfigurationManager::loadConfiguration();
    if (!ConfigurationManager::session.empty())
    {
        stringstream logLine;
        logLine << "login " << ConfigurationManager::session;
        LOG_debug << "Executing ... " << logLine.str();
        process_line((char *)logLine.str().c_str());
    }

    megacmd();
}

#endif //linux
