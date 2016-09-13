
#include "megacmdexecuter.h"
#include "megacmd.h"

#include "megacmdutils.h"
#include "configurationmanager.h"
#include "megacmdlogger.h"
#include "comunicationsmanager.h"
#include "listeners.h"
#include "megaapi_impl.h" //to use such things as MegaThread. It might be interesting to move the typedefs to a separate .h file

#include <iomanip>
#include <string>

#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>

using namespace mega;

static const char* rootnodenames[] = { "ROOT", "INBOX", "RUBBISH" };
static const char* rootnodepaths[] = { "/", "//in", "//bin" };

/**
 * @brief updateprompt updates prompt with the current user/location
 * @param api
 * @param handle
 */
void MegaCmdExecuter::updateprompt(MegaApi *api, MegaHandle handle){

    static char dynamicprompt[128];

    MegaNode *n = api->getNodeByHandle(handle);

    MegaUser *u = api->getMyUser();
    char *ptraux = dynamicprompt;
    char *lastpos = dynamicprompt+sizeof(dynamicprompt)/sizeof(dynamicprompt[0]);
    if (u)
    {
        const char *email = u->getEmail();
        strncpy(dynamicprompt,email,(lastpos-ptraux)/sizeof(dynamicprompt[0]));
        ptraux+=strlen(email);
        ptraux = min(ptraux,lastpos-2);
        delete u;
    }
    if (n)
    {
        char *np = api->getNodePath(n);
        *ptraux++=':';
        ptraux = min(ptraux,lastpos-2);
        strncpy(ptraux,np,(lastpos-ptraux)/sizeof(dynamicprompt[0]));
        ptraux += strlen(np);
        ptraux = min(ptraux,lastpos-2);
        delete n;
        delete np;

    }
    if (ptraux==dynamicprompt)
    {
        strcpy(ptraux,prompts[0]);
    }
    else
    {
        *ptraux++='$';
        ptraux = min(ptraux,lastpos-1);

        *ptraux++=' ';
        ptraux = min(ptraux,lastpos);

        *ptraux='\0';
    }

    changeprompt(dynamicprompt);
}


MegaCmdExecuter::MegaCmdExecuter(MegaApi *api, MegaCMDLogger *loggerCMD){
    this->api = api;
    this->loggerCMD = loggerCMD;
    fsAccessCMD = new MegaFileSystemAccess();
    mtxSyncMap.init(false);
}
MegaCmdExecuter::~MegaCmdExecuter(){
    delete fsAccessCMD;
}

// list available top-level nodes and contacts/incoming shares
void MegaCmdExecuter::listtrees()
{
    for (int i = 0; i < (int) (sizeof rootnodenames/sizeof *rootnodenames); i++)
    {
        OUTSTREAM << rootnodenames[i] << " on " << rootnodepaths[i] << endl;
        if (!api->isLoggedIn()) break; //only show /root
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

bool MegaCmdExecuter::includeIfIsExported(MegaApi *api, MegaNode * n, void *arg)
{
   if (n->isExported())
    {
        ((vector<MegaNode*> *) arg)->push_back(n->copy());
       return true;
    }
    return false;
}

bool MegaCmdExecuter::includeIfIsShared(MegaApi *api, MegaNode * n, void *arg)
{
    if (n->isShared())
    {
        ((vector<MegaNode*> *) arg)->push_back(n->copy());
        return true;
    }
    return false;
}

bool MegaCmdExecuter::includeIfIsPendingOutShare(MegaApi *api, MegaNode * n, void *arg)
{
    MegaShareList* pendingoutShares= api->getPendingOutShares(n);
    if(pendingoutShares && pendingoutShares->size())
    {
        ((vector<MegaNode*> *) arg)->push_back(n->copy());
        return true;
    }
    if(pendingoutShares)
        delete pendingoutShares;
    return false;
}


bool MegaCmdExecuter::includeIfIsSharedOrPendingOutShare(MegaApi *api, MegaNode * n, void *arg)
{
    if (n->isShared())
    {
        ((vector<MegaNode*> *) arg)->push_back(n->copy());
        return true;
    }
    MegaShareList* pendingoutShares= api->getPendingOutShares(n);
    if(pendingoutShares && pendingoutShares->size())
    {
        ((vector<MegaNode*> *) arg)->push_back(n->copy());
        return true;
    }
    if(pendingoutShares)
        delete pendingoutShares;
    return false;
}


bool MegaCmdExecuter::processTree(MegaNode *n, bool processor(MegaApi *, MegaNode *, void *),void *(arg))
{
    if (!n) return false;
    bool toret=true;
    MegaNodeList *children = api->getChildren(n);
    if (children){
        for (int i=0;i<children->size();i++)
        {
            bool childret = processTree(children->get(i),processor,arg);
            toret = toret && childret;
        }
        delete children;
    }

    bool currentret = processor(api, n,arg);
    return toret && currentret;
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
MegaNode* MegaCmdExecuter::nodebypath(const char* ptr, string* user, string* namepart)
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

        MegaUserList * usersList = api->getContacts();
        MegaUser *u = NULL;
        for (int i=0;i<usersList->size();i++)
        {
            if (usersList->get(i)->getEmail() == c[0])
            {
                 u=usersList->get(i);
                 break;
            }
        }
        if (u)
        {
            MegaNodeList* inshares = api->getInShares(u);
            for (int i=0;i<inshares->size();i++)
            {
                if (inshares->get(i)->getName() == c[1])
                {
                    n=inshares->get(i)->copy();
                    l=2;
                    break;
                }
            }
            delete inshares;
        }
        delete usersList;

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
                n = api->getRootNode();
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

/**
  TODO: doc. delete of added MegaNodes * into nodesMatching responsibility for the caller
 * @brief getNodesMatching
 * @param parentNode
 * @param c
 * @param nodesMatching
 */
void MegaCmdExecuter::getNodesMatching(MegaNode *parentNode, queue<string> pathParts, vector<MegaNode *> *nodesMatching)
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

MegaNode * MegaCmdExecuter::getRootNodeByPath(const char *ptr, string* user)
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
        MegaUserList * usersList = api->getContacts();
        MegaUser *u = NULL;
        for (int i=0;i<usersList->size();i++)
        {
            if (usersList->get(i)->getEmail() == c.front())
            {
                u=usersList->get(i);
                c.pop();
                break;
            }
        }
        if (u)
        {
            MegaNodeList* inshares = api->getInShares(u);
            for (int i=0;i<inshares->size();i++)
            {
                if (inshares->get(i)->getName() == c.front())
                {
                    n=inshares->get(i)->copy();
                    c.pop();
                    break;
                }
            }
            delete inshares;
        }
        delete usersList;
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
vector <MegaNode*> * MegaCmdExecuter::nodesbypath(const char* ptr, string* user, string* namepart)
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

        MegaUserList * usersList = api->getContacts();
        MegaUser *u = NULL;
        for (int i=0;i<usersList->size();i++)
        {
            if (usersList->get(i)->getEmail() == c.front())
            {
                u=usersList->get(i);
                c.pop();
                break;
            }
        }
        if (u)
        {
            MegaNodeList* inshares = api->getInShares(u);
            for (int i=0;i<inshares->size();i++)
            {
                if (inshares->get(i)->getName() == c.front())
                {
                    n=inshares->get(i)->copy();
                    c.pop();
                    break;
                }
            }
            delete inshares;
        }
        delete usersList;
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

//static void MegaCmdExecuter::displaytransferdetails(Transfer* t, const char* action)
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
static void MegaCmdExecuter::syncstat(Sync* sync)
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

//static void MegaCmdExecuter::nodestats(int* c, const char* action)
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

void MegaCmdExecuter::dumpNode(MegaNode* n, int extended_info, int depth, const char* title)
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

            if (INVALID_HANDLE != n->getPublicHandle())
//            if (n->isExported())
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
                if (extended_info>1)
                {
                    char * publicLink = n->getPublicLink();
                    OUTSTREAM << ": " << publicLink;
                    if (n->getExpirationTime())
                    {
                        if (n->isExpired()) OUTSTREAM << " expired at ";
                        else OUTSTREAM << " expires at ";
                        OUTSTREAM << " at " << getReadableTime(n->getExpirationTime());
                    }
                    delete []publicLink;
                }
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
                    if (outShares->get(i)->getNodeHandle()==n->getHandle())
                    {
                        OUTSTREAM << ", shared with " << outShares->get(i)->getUser() << ", access "
                                  << getAccessLevelStr(outShares->get(i)->getAccess());
                    }
                }
                MegaShareList* pendingoutShares= api->getPendingOutShares(n);
                if(pendingoutShares)
                {
                    for (int i=0;i<pendingoutShares->size();i++)
                    {
                        if (pendingoutShares->get(i)->getNodeHandle()==n->getHandle())
                        {
                            OUTSTREAM << ", shared (still pending)";
                            if (pendingoutShares->get(i)->getUser())
                                OUTSTREAM << " with " << pendingoutShares->get(i)->getUser();
                            OUTSTREAM << " access " << getAccessLevelStr(pendingoutShares->get(i)->getAccess());
                        }
                    }
                    delete pendingoutShares;
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
                    if (extended_info>1)
                    {
                        char * publicLink = n->getPublicLink();
                        OUTSTREAM << ": " << publicLink;
                        delete []publicLink;
                    }
                }
                delete outShares;
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

void MegaCmdExecuter::dumptree(MegaNode* n, int recurse, int extended_info, int depth, string pathRelativeTo)
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

void MegaCmdExecuter::nodepath(handle h, string* path)
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
                    string suser=getUserInSharedNode(n,api);

                    if (suser.size())
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

string MegaCmdExecuter::getDisplayPath(string givenPath, MegaNode* n)
{
    char * pathToNode = api->getNodePath(n);
    char * pathToShow=pathToNode;

    string pathRelativeTo = "NULL";
    string cwpath;

    if (givenPath.find('/') == 0)
    {
        pathRelativeTo="";
    }
    else{
        nodepath(cwd, &cwpath); //TODO: cwpath could be taken as argument
        if (cwpath=="/")
            pathRelativeTo = cwpath;
        else
            pathRelativeTo = cwpath+"/";
    }

    if (""==givenPath && !strcmp(pathToNode,cwpath.c_str())) { pathToNode[0]= '.'; pathToNode[1]= '\0';}

    if (pathRelativeTo != "")
        pathToShow = strstr(pathToNode,pathRelativeTo.c_str());

        if (pathToShow == pathToNode) //found at beginning
        {
            pathToShow+=pathRelativeTo.size();
        }
        else
            pathToShow=pathToNode;

    string toret(pathToShow);
    delete []pathToNode;
    return toret;
}

void MegaCmdExecuter::dumpListOfExported(MegaNode* n, string givenPath)
{
    vector<MegaNode *> listOfExported;
    processTree(n,includeIfIsExported,(void *)&listOfExported);
    for (std::vector< MegaNode * >::iterator it = listOfExported.begin() ; it != listOfExported.end(); ++it)
    {
        MegaNode * n = *it;
        if (n)
        {
            string pathToShow = getDisplayPath(givenPath, n);
            dumpNode(n, 2, 1,pathToShow.c_str());//,rNpath); //TODO: poner rNpath adecuado

            delete n;
        }
    }
    listOfExported.clear();
}

/**
 * @brief listnodeshares For a node, it prints all the shares it has
 * @param n
 * @param name
 */
void MegaCmdExecuter::listnodeshares(MegaNode* n, string name)
{
    MegaShareList* outShares=api->getOutShares(n);
    if(outShares)
    {
        for (int i=0;i<outShares->size();i++)
        {

            OUTSTREAM << name?name:n->getName();

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

void MegaCmdExecuter::dumpListOfShared(MegaNode* n, string givenPath)
{
    vector<MegaNode *> listOfShared;
    processTree(n,includeIfIsShared,(void *)&listOfShared);
    for (std::vector< MegaNode * >::iterator it = listOfShared.begin() ; it != listOfShared.end(); ++it)
    {
        MegaNode * n = *it;
        if (n)
        {
            string pathToShow = getDisplayPath(givenPath, n);
            //dumpNode(n, 3, 1,pathToShow.c_str());
            listnodeshares(n,pathToShow);

            delete n;
        }
    }
    listOfShared.clear();
}

//includes pending and normal shares
void MegaCmdExecuter::dumpListOfAllShared(MegaNode* n, string givenPath)
{
    vector<MegaNode *> listOfShared;
    processTree(n,includeIfIsSharedOrPendingOutShare,(void *)&listOfShared);
    for (std::vector< MegaNode * >::iterator it = listOfShared.begin() ; it != listOfShared.end(); ++it)
    {
        MegaNode * n = *it;
        if (n)
        {
            string pathToShow = getDisplayPath(givenPath, n);
            dumpNode(n, 3, 1,pathToShow.c_str());
            //notice: some nodes may be dumped twice

            delete n;
        }
    }
    listOfShared.clear();
}

void MegaCmdExecuter::dumpListOfPendingShares(MegaNode* n, string givenPath)
{
    vector<MegaNode *> listOfShared;
    processTree(n,includeIfIsPendingOutShare,(void *)&listOfShared);

    for (std::vector< MegaNode * >::iterator it = listOfShared.begin() ; it != listOfShared.end(); ++it)
    {
        MegaNode * n = *it;
        if (n)
        {
            string pathToShow = getDisplayPath(givenPath, n);
            dumpNode(n, 3, 1,pathToShow.c_str());

            delete n;
        }
    }
    listOfShared.clear();
}


void MegaCmdExecuter::loginWithPassword(char *password)
{
    MegaCmdListener *megaCmdListener = new MegaCmdListener(NULL);
    api->login(login.c_str(), password,megaCmdListener);
    actUponLogin(megaCmdListener);
    delete megaCmdListener;
}


//appfile_list appxferq[2];

int MegaCmdExecuter::loadfile(string* name, string* data)
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

void MegaCmdExecuter::actUponGetExtendedAccountDetails(SynchronousRequestListener *srl,int timeout)
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
                    strftime(timebuf, sizeof timebuf, "%c", localtime(&ts)); //TODO: do this with OUTSTREAM
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

bool MegaCmdExecuter::actUponFetchNodes(MegaApi *api, SynchronousRequestListener *srl,int timeout)
{
    if (timeout==-1)
        srl->wait();
    else
    {
        int trywaitout=srl->trywait(timeout);
        if (trywaitout){
           LOG_err << "Fetch nodes took too long, it may have failed. No further actions performed";
           return false;
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
        updateprompt(api,cwd);
        LOG_debug << " Fetch nodes correctly";
        return true;
    }
    else
    {
        LOG_err << " failed to fetch nodes. Error: " << srl->getError()->getErrorString();
    }
    return false;
}


void MegaCmdExecuter::actUponLogin(SynchronousRequestListener *srl,int timeout)
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
        actUponFetchNodes(api, srl,timeout);//TODO: should more accurately be max(0,timeout-timespent)
    }
    else //TODO: complete error control
    {
        LOG_err << "Login failed: " << srl->getError()->getErrorString();
    }
}

void MegaCmdExecuter::actUponLogout(SynchronousRequestListener *srl,int timeout)
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
        ConfigurationManager::saveSession("");
    }
    else
    {
        LOG_err << "actUponLogout failed to logout: " << srl->getError()->getErrorString();
    }
    updateprompt(api,cwd);
}

int MegaCmdExecuter::actUponCreateFolder(SynchronousRequestListener *srl,int timeout)
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

int MegaCmdExecuter::actUponDeleteNode(SynchronousRequestListener *srl,int timeout)
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

void MegaCmdExecuter::downloadNode(string localPath, MegaApi* api, MegaNode *node)
{
    MegaCmdTransferListener *megaCmdTransferListener = new MegaCmdTransferListener(api,NULL);
    LOG_debug << "Starting download: " << node->getName() << " to : " << localPath;
    api->startDownload(node,localPath.c_str(),megaCmdTransferListener);
    megaCmdTransferListener->wait();
    if (megaCmdTransferListener->getError() && megaCmdTransferListener->getError()->getErrorCode() == MegaError::API_OK)
    {
        LOG_info << "Download complete: " << localPath << megaCmdTransferListener->getTransfer()->getFileName();
    }
    else
    {
        if (megaCmdTransferListener->getError())
            LOG_err << "Download failed: " << megaCmdTransferListener->getError()->getErrorString();
        else
            LOG_err << "Download failed";
    }
    delete megaCmdTransferListener;
}

void MegaCmdExecuter::uploadNode(string localPath, MegaApi* api, MegaNode *node)
{
    MegaCmdTransferListener *megaCmdTransferListener = new MegaCmdTransferListener(api,NULL);
    LOG_debug << "Starting download: " << node->getName() << " to : " << localPath;
    api->startUpload(localPath.c_str(),node,megaCmdTransferListener);
    megaCmdTransferListener->wait();
    //TODO: process errors
    if (megaCmdTransferListener->getError() && megaCmdTransferListener->getError()->getErrorCode() == MegaError::API_OK)
    {
        char * destinyPath=api->getNodePath(node);
        LOG_info << "Upload complete: " << megaCmdTransferListener->getTransfer()->getFileName() << " to " << destinyPath;
        delete []destinyPath;
    }
    else
    {
        LOG_err << "Upload failed: " << megaCmdTransferListener->getError()->getErrorString();
    }
    delete megaCmdTransferListener;
}

void MegaCmdExecuter::exportNode(MegaNode *n,int expireTime)
{
    MegaCmdListener *megaCmdListener = new MegaCmdListener(api,NULL);

    api->exportNode(n,expireTime,megaCmdListener);
    megaCmdListener->wait();
    if (megaCmdListener->getError() && megaCmdListener->getError()->getErrorCode() == MegaError::API_OK)
    {
        MegaNode *nexported = api->getNodeByHandle(megaCmdListener->getRequest()->getNodeHandle());
        if (nexported)
        {
            char *nodepath = api->getNodePath(nexported);
            OUTSTREAM << "Exported " << nodepath << " : "  << nexported->getPublicLink();
            if (nexported->getExpirationTime())
            {
                OUTSTREAM << " expires at " << getReadableTime(nexported->getExpirationTime());
            }
            OUTSTREAM << endl;
            delete[] nodepath;
            delete nexported;
        }
        else{
            setCurrentOutCode(2);
            LOG_err << "Exported node not found!" ;
        }
    }
    else
    {
        //TODO: deal with errors
        if (megaCmdListener->getError())
        {
            setCurrentOutCode(megaCmdListener->getError()->getErrorCode());
            OUTSTREAM << "Could not exportNode" << megaCmdListener->getError()->getErrorString() << endl;
        }
        else
        {
            setCurrentOutCode(3);
            LOG_fatal << "Empty error at exportNode" ;
        }

    }
    delete megaCmdListener;
}

void MegaCmdExecuter::disableExport(MegaNode *n)
{
    if (!n->isExported())
    {
        setCurrentOutCode(3);
        OUTSTREAM << "Could not disable export: node not exported." << endl;
        return;
    }
    MegaCmdListener *megaCmdListener = new MegaCmdListener(api,NULL);

    api->disableExport(n,megaCmdListener);
    megaCmdListener->wait();
    if (megaCmdListener->getError() && megaCmdListener->getError()->getErrorCode() == MegaError::API_OK)
    {
        MegaNode *nexported = api->getNodeByHandle(megaCmdListener->getRequest()->getNodeHandle());
        if (nexported)
        {
            char *nodepath = api->getNodePath(nexported);
            OUTSTREAM << "Disabled export " << nodepath << " : "  << nexported->getPublicLink() << endl;
            delete[] nodepath;
            delete nexported;
        }
        else{
            setCurrentOutCode(2);
            LOG_err << "Exported node not found!" ;
        }
    }
    else
    {
        //TODO: deal with errors
        if (megaCmdListener->getError())
        {
            setCurrentOutCode(megaCmdListener->getError()->getErrorCode());
            OUTSTREAM << "Could not disable export: " << megaCmdListener->getError()->getErrorString() << endl;
        }
        else
        {
            setCurrentOutCode(3);
            LOG_fatal << "Empty error at disable Export" ;
        }
    }
    delete megaCmdListener;
}

void MegaCmdExecuter::shareNode(MegaNode *n,string with,int level)
{
    MegaCmdListener *megaCmdListener = new MegaCmdListener(api,NULL);

    api->share(n,with.c_str(),level,megaCmdListener);
    megaCmdListener->wait();
    if (megaCmdListener->getError() && megaCmdListener->getError()->getErrorCode() == MegaError::API_OK)
    {
        MegaNode *nshared = api->getNodeByHandle(megaCmdListener->getRequest()->getNodeHandle());
        if (nshared)
        {
            char *nodepath = api->getNodePath(nshared);
            if (megaCmdListener->getRequest()->getAccess()==MegaShare::ACCESS_UNKNOWN)
            {
                OUTSTREAM << "Stopped sharing " << nodepath << " with "  << megaCmdListener->getRequest()->getEmail() << endl;
            }
            else
            {
                OUTSTREAM << "Shared " << nodepath << " : "  << megaCmdListener->getRequest()->getEmail()
                          << " accessLevel="<<megaCmdListener->getRequest()->getAccess() << endl;
            }
            delete[] nodepath;
            delete nshared;
        }
        else{
            setCurrentOutCode(2);
            LOG_err << "Shared node not found!" ;
        }
    }
    else
    {
        if (megaCmdListener->getError())
        {
            setCurrentOutCode(megaCmdListener->getError()->getErrorCode());
            OUTSTREAM << "Could not share node" << megaCmdListener->getError()->getErrorString() << endl;
        }
        else
        {
            setCurrentOutCode(3);
            LOG_fatal << "Empty error at shareNode" ;
        }

    }
    delete megaCmdListener;
}

void MegaCmdExecuter::disableShare(MegaNode *n, string with)
{
   shareNode(n,with,MegaShare::ACCESS_UNKNOWN);
}

void MegaCmdExecuter::executecommand(vector<string> words,map<string,int> &clflags,map<string,string> &cloptions)
{
    //TODO: flags and options as pointer rather than reference
    MegaNode* n;

    if (words[0] == "ls")
    {
        int recursive = getFlag(&clflags,"R") + getFlag(&clflags,"r") ;
        int extended_info = getFlag(&clflags,"l");

        if ((int) words.size() > 1)
        {
            string rNpath = "NULL";
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

            if (hasWildCards(words[1]))
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

                    updateprompt(api, cwd);
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
                if (hasWildCards(words[i]))
                {
                    vector<MegaNode *> *nodesToDelete = nodesbypath(words[i].c_str());
                    for (std::vector< MegaNode * >::iterator it = nodesToDelete->begin() ; it != nodesToDelete->end(); ++it)
                    {
                        MegaNode * nodeToDelete = *it;
                        if (nodeToDelete)
                        {
                            LOG_verbose << "Deleting recursively: " << words[i];
                            MegaCmdListener *megaCmdListener = new MegaCmdListener(NULL);
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
                        MegaCmdListener *megaCmdListener = new MegaCmdListener(NULL);
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
                                MegaCmdListener *megaCmdListener = new MegaCmdListener(NULL);
                                api->moveNode(n,tn,megaCmdListener);
                                megaCmdListener->wait(); // TODO: act upon move. log access denied...
                                if (megaCmdListener->getError() && megaCmdListener->getError()->getErrorCode() == MegaError::API_OK)
                                {
                                    MegaCmdListener *megaCmdListener = new MegaCmdListener(NULL);
                                    api->renameNode(n,newname.c_str(),megaCmdListener);
                                    megaCmdListener->wait(); // TODO: act upon rename. log access denied...
                                    delete megaCmdListener;
                                }
                                else
                                {
                                    LOG_err << "Won't rename, since move failed " << n->getName() <<" to " << tn->getName() << " : " << megaCmdListener->getError()->getErrorCode();
                                }
                                delete megaCmdListener;
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
                                    MegaCmdListener *megaCmdListener = new MegaCmdListener(NULL);
                                    api->moveNode(n,api->getNodeByHandle(tn->getParentHandle()),megaCmdListener);
                                    megaCmdListener->wait(); //TODO: do actuponmove...
                                    if (megaCmdListener->getError() && megaCmdListener->getError()->getErrorCode() != MegaError::API_OK)
                                    {
                                        LOG_err << "Failed to move node: " << megaCmdListener->getError()->getErrorString();
                                    }
                                    else
                                    {
                                        const char* name_to_replace = tn->getName();

                                        //remove (replaced) target node
                                        if (n != tn) //just in case moving to same location
                                        {
                                            MegaCmdListener *megaCmdListener = new MegaCmdListener(NULL);
                                            api->remove(tn,megaCmdListener); //remove target node
                                            megaCmdListener->wait(); //TODO: actuponremove ...
                                            if (megaCmdListener->getError() && megaCmdListener->getError()->getErrorCode() != MegaError::API_OK)
                                            {
                                                LOG_err << "Couldnt move " << n->getName() <<" to " << tn->getName() << " : " << megaCmdListener->getError()->getErrorCode();
                                            }
                                            delete megaCmdListener;
                                        }

                                        // rename moved node with the new name
                                        if (megaCmdListener->getError() && megaCmdListener->getError()->getErrorCode() == MegaError::API_OK)
                                        {
                                            if (!strcmp(name_to_replace,n->getName()))
                                            {
                                                MegaCmdListener *megaCmdListener = new MegaCmdListener(NULL);
                                                api->renameNode(n,name_to_replace,megaCmdListener);
                                                megaCmdListener->wait(); // TODO: act upon rename. log access denied...
                                                if (megaCmdListener->getError() && megaCmdListener->getError()->getErrorCode() != MegaError::API_OK)
                                                {
                                                    LOG_err << "Failed to rename moved node: " << megaCmdListener->getError()->getErrorString();
                                                }
                                                delete megaCmdListener;
                                            }
                                        }
                                        else
                                        {
                                            LOG_err << "Won't rename, since move failed " << n->getName() <<" to " << tn->getName() << " : " << megaCmdListener->getError()->getErrorCode();
                                        }
                                    }
                                    delete megaCmdListener;
                                }
                                else
                                {
                                    LOG_fatal << "Destiny node is orphan!!!";
                                }
                            }
                            else // target is a folder
                            {
                                MegaCmdListener *megaCmdListener = new MegaCmdListener(NULL);
                                api->moveNode(n,tn,megaCmdListener);
                                megaCmdListener->wait();
                                if (megaCmdListener->getError() && megaCmdListener->getError()->getErrorCode() != MegaError::API_OK)
                                {
                                    LOG_err << "Failed to move node: " << megaCmdListener->getError()->getErrorString();
                                }
                                delete megaCmdListener;
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
                                MegaCmdListener *megaCmdListener = new MegaCmdListener(NULL);
                                api->copyNode(n,tn,newname.c_str(),megaCmdListener); //only works for files
                                megaCmdListener->wait();
                                if (megaCmdListener->getError() && megaCmdListener->getError()->getErrorCode() != MegaError::API_OK)
                                {
                                    LOG_err << "Failed to copy node: " << megaCmdListener->getError()->getErrorString();
                                }
                                delete megaCmdListener;

                                //TODO: newname is ignored in case of public node!!!!
                            }
                            else//copy & rename
                            {
                                //copy with new name
                                MegaCmdListener *megaCmdListener = new MegaCmdListener(NULL);
                                api->copyNode(n,tn,megaCmdListener);
                                megaCmdListener->wait();//TODO: actupon...
                                if (megaCmdListener->getError() && megaCmdListener->getError()->getErrorCode() == MegaError::API_OK)
                                {
                                    MegaNode * newNode=api->getNodeByHandle(megaCmdListener->getRequest()->getNodeHandle());
                                    if (newNode)
                                    {
                                        MegaCmdListener *megaCmdListener = new MegaCmdListener(NULL);
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
                                else
                                {
                                    LOG_err << "Failed to copy node: " << megaCmdListener->getError()->getErrorString();
                                }
                                delete megaCmdListener;


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
                                        MegaCmdListener *megaCmdListener = new MegaCmdListener(NULL);
                                        api->copyNode(n,tnParentNode,name_to_replace,megaCmdListener);
                                        megaCmdListener->wait();//TODO: actupon...
                                        delete megaCmdListener;
                                        delete tnParentNode;

                                        //remove target node
                                        megaCmdListener = new MegaCmdListener(NULL);
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
                                MegaCmdListener *megaCmdListener = new MegaCmdListener(NULL);
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
                    MegaCmdListener *megaCmdListener = new MegaCmdListener(NULL);

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
                    apiFolder->setAccountAuth(api->getAccountAuth());

                    MegaCmdListener *megaCmdListener = new MegaCmdListener(apiFolder,NULL);
                    apiFolder->loginToFolder(words[1].c_str(),megaCmdListener);
                    megaCmdListener->wait();
                    if (megaCmdListener->getError()->getErrorCode() == MegaError::API_OK)
                    {
                        MegaCmdListener *megaCmdListener2 = new MegaCmdListener(apiFolder,NULL);
                        apiFolder->fetchNodes(megaCmdListener2);
                        megaCmdListener2->wait();
                        if(megaCmdListener2->getError()->getErrorCode() == MegaError::API_OK)
                        {
                            MegaNode *folderRootNode = apiFolder->getRootNode();
                            if (folderRootNode)
                            {
                                MegaNode *authorizedNode = apiFolder->authorizeNode(folderRootNode);
                                if (authorizedNode !=NULL)
                                {
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
                            else
                            {
                                LOG_err << "Couldn't get root folder for folder link";
                            }
                        }
                        else
                        {
                            setCurrentOutCode(megaCmdListener2->getError()->getErrorCode());
                            OUTSTREAM << "Failed to access folder link, perhaps link is incorrect" << endl;
                        }
                        delete megaCmdListener2;

                    }
                    else{
                        LOG_err << "Failed to login to folder: " << megaCmdListener->getError()->getErrorCode() ;
                    }
                    delete megaCmdListener;

                    //                                MegaCmdListener *megaCmdListenerLogout = new MegaCmdListener(apiFolder,NULL);
                    //                                apiFolder->logout(megaCmdListenerLogout);
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
                if (hasWildCards(words[1]))
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
                    MegaCmdListener *megaCmdListener = new MegaCmdListener(NULL);

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
                        ConfigurationManager::loadedSyncs[megaCmdListener->getRequest()->getFile()] = thesync;

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
            for(itr = ConfigurationManager::loadedSyncs.begin(); itr != ConfigurationManager::loadedSyncs.end(); i++)
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
                            MegaCmdListener *megaCmdListener = new MegaCmdListener(NULL);
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
                            MegaCmdListener *megaCmdListener = new MegaCmdListener(NULL);
                            if (thesync->active)  //if not active, removeSync will fail.)
                            {
                                api->removeSync(n,megaCmdListener);
                                megaCmdListener->wait();//TODO: actupon...
                                if (megaCmdListener->getError() && megaCmdListener->getError()->getErrorCode() == MegaError::API_OK)
                                {
                                    ConfigurationManager::loadedSyncs.erase(itr++);
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
                                ConfigurationManager::loadedSyncs.erase(itr++);
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
            for(itr = ConfigurationManager::loadedSyncs.begin(); itr != ConfigurationManager::loadedSyncs.end(); ++itr)
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
        ConfigurationManager::saveSyncs(ConfigurationManager::loadedSyncs);
        mtxSyncMap.unlock();
        return;
    }
#endif
    else if (words[0] == "login")
    {
        if (!api->isLoggedIn())
        {
            if (words.size() > 1)
            {
                if (strchr(words[1].c_str(), '@'))
                {
                    // full account login
                    if (words.size() > 2)
                    {
                        //TODO: validate & delete
                        MegaCmdListener *megaCmdListener = new MegaCmdListener(NULL);
                        api->login(words[1].c_str(),words[2].c_str(),megaCmdListener);
                        actUponLogin(megaCmdListener);
                        delete megaCmdListener;
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
                        MegaCmdListener *megaCmdListener = new MegaCmdListener(NULL);
                        api->loginToFolder(words[1].c_str(),megaCmdListener);
                        actUponLogin(megaCmdListener);
                        delete megaCmdListener;
                        return;
                    }
                    else
                    {
                        byte session[64];

                        if (words[1].size() < sizeof session * 4 / 3)
                        {
                            OUTSTREAM << "Resuming session..." << endl;
                            MegaCmdListener *megaCmdListener = new MegaCmdListener(NULL);
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
        string with = getOption(&cloptions,"with","");
        if ((getFlag(&clflags,"a") || getFlag(&clflags,"d") )&& "" == with)
        {
            setCurrentOutCode(2);
            OUTSTREAM << " Required --with destiny" << endl << getUsageStr("share") << endl;
            return;
        }
        int level = getintOption(&cloptions,"access-level",MegaShare::ACCESS_READ);
        bool listPending = getFlag(&clflags,"p");

        if (words.size()<=1) words.push_back(string("")); //give at least an empty so that cwd is used

        for (int i=1;i<words.size();i++)
        {
            if (hasWildCards(words[i]))
            {
                vector<MegaNode *> *nodes = nodesbypath(words[i].c_str());
                if (nodes)
                {
                    if (!nodes->size())
                    {
                        setCurrentOutCode(2);
                        OUTSTREAM << "Nodes not found: " << words[i] << endl;
                    }
                    for (std::vector< MegaNode * >::iterator it = nodes->begin() ; it != nodes->end(); ++it)
                    {
                        MegaNode * n = *it;
                        if (n)
                        {
                            if (getFlag(&clflags,"a") )
                            {
                                LOG_debug << " sharing ... " << n->getName() << " with " << with;
                                shareNode(n,with,level);
                            }
                            else if (getFlag(&clflags,"d") )
                            {
                                LOG_debug << " deleting share ... " << n->getName();
                                disableShare(n, with);
                                //TODO: disable with all
                            }
                            else
                            {
                                if (listPending)
                                    dumpListOfPendingShares(n, words[i]);
                                else
                                    dumpListOfShared(n, words[i]);
                            }
                            delete n;
                        }
                    }
                    nodes->clear();
                    delete nodes ;
                }
                else
                {
                    setCurrentOutCode(2);
                    OUTSTREAM << "Node not found: " << words[i] << endl;
                }
            }
            else // non-wildcard
            {
                MegaNode *n = nodebypath(words[i].c_str());
                if (n)
                {
                    if (getFlag(&clflags,"a") )
                    {
                        LOG_debug << " sharing ... " << n->getName() << " with " << with;
                        shareNode(n,with,level);
                    }
                    else if (getFlag(&clflags,"d") )
                    {
                        LOG_debug << " deleting share ... " << n->getName();
                        disableShare(n, with);
                    }
                    else
                    {
                        if (listPending)
                            dumpListOfPendingShares(n, words[i]);
                        else
                            dumpListOfShared(n, words[i]);
                    }                    delete n;
                }
                else
                {
                    setCurrentOutCode(2);
                    OUTSTREAM << "Node not found: " << words[i] << endl;
                }
            }
        }


        return;
    }
    else if (words[0] == "users")
    {
        MegaUserList* usersList=api->getContacts();
        if (usersList){
            for (int i=0;i<usersList->size(); i++){
                MegaUser *user = usersList->get(i);
                OUTSTREAM << user->getEmail() << ", " << visibilityToString(user->getVisibility());
                if (user->getTimestamp()) OUTSTREAM << " since " << getReadableTime(user->getTimestamp());
                OUTSTREAM << endl;
                if (getFlag(&clflags,"s") )
                {
                    MegaShareList *shares = api->getOutShares();
                    if (shares)
                    {
                        bool first_share = true;
                        for (int j=0;j<shares->size();j++)
                        {
                            if (!strcmp(shares->get(j)->getUser(),user->getEmail()))
                            {
                                MegaNode * n = api->getNodeByHandle(shares->get(j)->getNodeHandle() );
                                if (n)
                                {
                                    if (first_share)
                                    {
                                        OUTSTREAM << "\tSharing:" << endl;
                                        first_share=false;
                                    }

                                    OUTSTREAM << "\t";
                                    dumpNode(n,2,0,getDisplayPath("/",n).c_str());
                                    delete n;
                                }
                            }
                        }
                        delete shares;
                    }
                }
            }
            delete usersList;
        }

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
                            if (!getFlag(&clflags,"p") && !lastleave )
                            {
                                setCurrentOutCode(2);
                                OUTSTREAM << "Use -p to create folders recursively" << endl;
                                delete currentnode;
                                return;
                            }
                            LOG_verbose << "Creating (sub)folder: " << newfoldername;
                            MegaCmdListener *megaCmdListener = new MegaCmdListener(NULL);
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
        if (words.size()>1)
        {
            string email = words[1];
            //TODO: add email validation
            if (email.find("@") == string::npos
                    || email.find(".") == string::npos
                    || (email.find("@") >  email.find(".") ) )
            {
                OUTSTREAM << "No valid email provided" << endl;
                OUTSTREAM << "      "<< getUsageStr("invite") << endl;
            }
            else
            {
                int action = MegaContactRequest::INVITE_ACTION_ADD;
                if (getFlag(&clflags,"d") ) action = MegaContactRequest::INVITE_ACTION_DELETE;
                if (getFlag(&clflags,"r") ) action = MegaContactRequest::INVITE_ACTION_REMIND;

                string message = getOption(&cloptions,"message","");
                MegaCmdListener *megaCmdListener = new MegaCmdListener(NULL);
                api->inviteContact(email.c_str(),message.c_str(),action,megaCmdListener);
                megaCmdListener->wait();
                if (megaCmdListener->getError()->getErrorCode() == MegaError::API_OK)
                {
                    OUTSTREAM << "Invitation sent to user: " << email << endl;
                }
                else if (megaCmdListener->getError()->getErrorCode() == MegaError::API_EACCESS)
                {
                    setCurrentOutCode(megaCmdListener->getError()->getErrorCode());
                    OUTSTREAM << "Reminder not yet available: " << " available after 15 days" << endl;
                    //TODO:  output time when remiender will be available << getReadableTime(getTimeStampAfter(GETCRTIMESTAMP),"15d")) ))
                }
                else{

                    setCurrentOutCode(megaCmdListener->getError()->getErrorCode());
                    OUTSTREAM << "Failed to invite " << email << ": " << megaCmdListener->getError()->getErrorString() << endl;
                }
                delete megaCmdListener;
            }
        }

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
            if (getFlag(&clflags,"l"))
            {
                MegaCmdListener *megaCmdListener = new MegaCmdListener(NULL);
                api->getExtendedAccountDetails(true,true,true,megaCmdListener);//TODO: continue this.
                actUponGetExtendedAccountDetails(megaCmdListener);
                delete megaCmdListener;
            }
            delete u;
        }
        else
        {
            OUTSTREAM << "Not logged in." << endl;
        }

        return;
    }
    else if (words[0] == "export")
    {

        MegaNode * n;
        time_t expireTime = 0;
        string sexpireTime = getOption(&cloptions,"expire","");
        if ("" != sexpireTime) expireTime = getTimeStampAfter(sexpireTime);
        if (expireTime < 0)
        {
            setCurrentOutCode(2);
            OUTSTREAM << "Invalid time " << sexpireTime << endl;
            return;
        }

        if (words.size()<=1) words.push_back(string("")); //give at least an empty so that cwd is used

        for (int i=1;i<words.size();i++)
        {
            if (hasWildCards(words[i]))
            {
                vector<MegaNode *> *nodes = nodesbypath(words[i].c_str());
                if (nodes)
                {
                    if (!nodes->size())
                    {
                        setCurrentOutCode(2);
                        OUTSTREAM << "Nodes not found: " << words[i] << endl;
                    }
                    for (std::vector< MegaNode * >::iterator it = nodes->begin() ; it != nodes->end(); ++it)
                    {
                        MegaNode * n = *it;
                        if (n)
                        {
                            if (getFlag(&clflags,"a") )
                            {
                                LOG_debug << " exporting ... " << n->getName() << " expireTime=" << expireTime;
                                exportNode(n,expireTime);
                            }
                            else if (getFlag(&clflags,"d") )
                            {
                                LOG_debug << " deleting export ... " << n->getName();
                                disableExport(n);
                            }
                            else
                                dumpListOfExported(n, words[i]);
                            delete n;
                        }
                    }
                    nodes->clear();
                    delete nodes ;
                }
                else
                {
                    setCurrentOutCode(2);
                    OUTSTREAM << "Node not found: " << words[i] << endl;
                }
            }
            else
            {
                MegaNode *n = nodebypath(words[i].c_str());
                if (n)
                {
                    if (getFlag(&clflags,"a") )
                    {
                        LOG_debug << " exporting ... " << n->getName();
                        exportNode(n,expireTime);
                    }
                    else if (getFlag(&clflags,"d") )
                    {
                        LOG_debug << " deleting export ... " << n->getName();
                        disableExport(n);
                    }
                    else
                        dumpListOfExported(n, words[i]);
                    delete n;
                }
                else
                {
                    setCurrentOutCode(2);
                    OUTSTREAM << "Node not found: " << words[i] << endl;
                }
            }
        }

        return;
    }
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
        MegaCmdListener *megaCmdListener = new MegaCmdListener(NULL);
        api->fetchNodes(megaCmdListener);
        actUponFetchNodes(api, megaCmdListener);
        delete megaCmdListener;
        return;
    }
    else if (words[0] == "logout")
    {
        OUTSTREAM << "Logging off..." << endl;
        MegaCmdListener *megaCmdListener = new MegaCmdListener(NULL);
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

//    else if (words[0] == "confirm")
//    {
//        if (signupemail.size() && signupcode.size())
//        {
//            OUTSTREAM << "Please type " << signupemail << "'s password to confirm the signup." << endl;
//            setprompt(LOGINPASSWORD);
//        }
//        else
//        {
//            OUTSTREAM << "No signup confirmation pending." << endl;
//        }

//        return;
//    }
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
    else if (words[0] == "history")
    {
        printHistory();
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
        MegaContactRequestList *ocrl = api->getOutgoingContactRequests();
        if (ocrl && ocrl->size()){
            OUTSTREAM << "Outgoing PCRs:" << endl;
            for (int i=0;i<ocrl->size();i++)
            {
                MegaContactRequest * cr = ocrl->get(i);
                OUTSTREAM << " " <<  setw(22)  << cr->getTargetEmail();

                MegaHandle id = cr->getHandle();
                char sid[12];
                Base64::btoa((byte*)&(id), sizeof(id), sid);

                OUTSTREAM << "\t (id: " << sid << ", creation: " << getReadableTime(cr->getCreationTime())
                          << ", modification: " << getReadableTime(cr->getModificationTime()) << ")";
                //                                OUTSTREAM << ": " << cr->getSourceMessage();

                OUTSTREAM << endl;
            }
            delete ocrl;
        }
        MegaContactRequestList *icrl = api->getIncomingContactRequests();
        if (icrl && icrl->size()){
            OUTSTREAM << "Incoming PCRs:" << endl;

            for (int i=0;i<icrl->size();i++)
            {
                MegaContactRequest * cr = icrl->get(i);
                OUTSTREAM << " " << setw(22) << cr->getSourceEmail();

                MegaHandle id = cr->getHandle();
                char sid[12];
                Base64::btoa((byte*)&(id), sizeof(id), sid);

                OUTSTREAM << "\t (id: " << sid << ", creation: " << getReadableTime(cr->getCreationTime())
                          << ", modification: " << getReadableTime(cr->getModificationTime()) << ")";
                if (cr->getSourceMessage())
                    OUTSTREAM << endl << "\t" << "Invitation message: " << cr->getSourceMessage();

                OUTSTREAM << endl;
            }
            delete icrl;
        }
        return;
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
    else
    {
        setCurrentOutCode(1);
        OUTSTREAM << "Invalid command:" << words[0]<<  endl;
    }
}
