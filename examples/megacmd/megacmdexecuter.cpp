/**
 * @file examples/megacmd/megacmd.cpp
 * @brief MegaCMD: Executer of the commands
 *
 * (c) 2013-2016 by Mega Limited, Auckland, New Zealand
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
    char *lastpos = dynamicprompt + sizeof( dynamicprompt ) / sizeof( dynamicprompt[0] );
    if (u)
    {
        const char *email = u->getEmail();
        strncpy(dynamicprompt, email, ( lastpos - ptraux ) / sizeof( dynamicprompt[0] ));
        ptraux += strlen(email);
        ptraux = min(ptraux, lastpos - 2);
        delete u;
    }
    if (n)
    {
        char *np = api->getNodePath(n);
        *ptraux++ = ':';
        ptraux = min(ptraux, lastpos - 2);
        strncpy(ptraux, np, ( lastpos - ptraux ) / sizeof( dynamicprompt[0] ));
        ptraux += strlen(np);
        ptraux = min(ptraux, lastpos - 2);
        delete n;
        delete []np;
    }
    if (ptraux == dynamicprompt)
    {
        strcpy(ptraux, prompts[0]);
    }
    else
    {
        *ptraux++ = '$';
        ptraux = min(ptraux, lastpos - 1);

        *ptraux++ = ' ';
        ptraux = min(ptraux, lastpos);

        *ptraux = '\0';
    }

    changeprompt(dynamicprompt);
}


MegaCmdExecuter::MegaCmdExecuter(MegaApi *api, MegaCMDLogger *loggerCMD){
    this->api = api;
    this->loggerCMD = loggerCMD;
    cwd = UNDEF;
    fsAccessCMD = new MegaFileSystemAccess();
    mtxSyncMap.init(false);
}
MegaCmdExecuter::~MegaCmdExecuter(){
    delete fsAccessCMD;
    delete []session;
}

// list available top-level nodes and contacts/incoming shares
void MegaCmdExecuter::listtrees()
{
    for (int i = 0; i < (int)( sizeof rootnodenames / sizeof *rootnodenames ); i++)
    {
        OUTSTREAM << rootnodenames[i] << " on " << rootnodepaths[i] << endl;
        if (!api->isLoggedIn())
        {
            break;                     //only show /root
        }
    }

    MegaShareList * msl = api->getInSharesList();
    for (int i = 0; i < msl->size(); i++)
    {
        MegaShare *share = msl->get(i);
        MegaNode *n = api->getNodeByHandle(share->getNodeHandle());

        OUTSTREAM << "INSHARE on " << share->getUser() << ":" << n->getName() << " (" << getAccessLevelStr(share->getAccess()) << ")" << endl;
        delete n;
    }

    delete ( msl );
}

bool MegaCmdExecuter::includeIfIsExported(MegaApi *api, MegaNode * n, void *arg)
{
    if (n->isExported())
    {
        (( vector<MegaNode*> * )arg )->push_back(n->copy());
        return true;
    }
    return false;
}

bool MegaCmdExecuter::includeIfIsShared(MegaApi *api, MegaNode * n, void *arg)
{
    if (n->isShared())
    {
        (( vector<MegaNode*> * )arg )->push_back(n->copy());
        return true;
    }
    return false;
}

bool MegaCmdExecuter::includeIfIsPendingOutShare(MegaApi *api, MegaNode * n, void *arg)
{
    MegaShareList* pendingoutShares = api->getPendingOutShares(n);
    if (pendingoutShares && pendingoutShares->size())
    {
        (( vector<MegaNode*> * )arg )->push_back(n->copy());
        return true;
    }
    if (pendingoutShares)
    {
        delete pendingoutShares;
    }
    return false;
}


bool MegaCmdExecuter::includeIfIsSharedOrPendingOutShare(MegaApi *api, MegaNode * n, void *arg)
{
    if (n->isShared())
    {
        (( vector<MegaNode*> * )arg )->push_back(n->copy());
        return true;
    }
    MegaShareList* pendingoutShares = api->getPendingOutShares(n);
    if (pendingoutShares && pendingoutShares->size())
    {
        (( vector<MegaNode*> * )arg )->push_back(n->copy());
        return true;
    }
    if (pendingoutShares)
    {
        delete pendingoutShares;
    }
    return false;
}

struct patternNodeVector{
   string pattern;
   vector<MegaNode*> *nodesMatching;
};


bool MegaCmdExecuter::includeIfMatchesPattern(MegaApi *api, MegaNode * n, void *arg)
{
    struct patternNodeVector *pnv = (struct patternNodeVector *) arg;
    if (patternMatches(n->getName(),pnv->pattern.c_str()) )
    {
        pnv->nodesMatching->push_back(n->copy());
        return true;
    }
    return false;
}

bool MegaCmdExecuter::processTree(MegaNode *n, bool processor(MegaApi *, MegaNode *, void *), void *( arg ))
{
    if (!n)
    {
        return false;
    }
    bool toret = true;
    MegaNodeList *children = api->getChildren(n);
    if (children)
    {
        for (int i = 0; i < children->size(); i++)
        {
            bool childret = processTree(children->get(i), processor, arg);
            toret = toret && childret;
        }

        delete children;
    }

    bool currentret = processor(api, n, arg);
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
    do
    {
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

                if (( *ptr == '/' ) || ( *ptr == ':' ) || !*ptr)
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
            else if (( *ptr & 0xf0 ) == 0xe0)
            {
                l = 1;
            }
            else if (( *ptr & 0xf8 ) == 0xf0)
            {
                l = 2;
            }
            else if (( *ptr & 0xfc ) == 0xf8)
            {
                l = 3;
            }
            else if (( *ptr & 0xfe ) == 0xfc)
            {
                l = 4;
            }
        }
        else
        {
            l--;
        }
    }
    while (*ptr++);

    if (l)
    {
        return NULL;
    }

    if (remote)
    {
        // target: user inbox - record username/email and return NULL
        if (( c.size() == 2 ) && !c[1].size())
        {
            if (user)
            {
                *user = c[0];
            }

            return NULL;
        }

        MegaUserList * usersList = api->getContacts();
        MegaUser *u = NULL;
        for (int i = 0; i < usersList->size(); i++)
        {
            if (usersList->get(i)->getEmail() == c[0])
            {
                u = usersList->get(i);
                break;
            }
        }

        if (u)
        {
            MegaNodeList* inshares = api->getInShares(u);
            for (int i = 0; i < inshares->size(); i++)
            {
                if (inshares->get(i)->getName() == c[1])
                {
                    n = inshares->get(i)->copy();
                    l = 2;
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
        if (( c.size() > 1 ) && !c[0].size())
        {
            // path starting with //
            if (( c.size() > 2 ) && !c[1].size())
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
                if (n != aux)
                {
                    delete aux;
                }
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
                        if (namepart && ( l == (int)c.size() - 1 )) //if this is the last part, we will pass that one, so that a mv command know the name to give the the new node
                        {
                            *namepart = c[l];
                            return n;
                        }

                        delete n;
                        return NULL;
                    }

                    if (n != nn)
                    {
                        delete n;
                    }
                    n = nn;
                }
            }
        }

        l++;
    }

    return n;
}

/**
 *  You take the ownership of the nodes added in nodesMatching
 * @brief getNodesMatching
 * @param parentNode
 * @param c
 * @param nodesMatching
 */
void MegaCmdExecuter::getNodesMatching(MegaNode *parentNode, queue<string> pathParts, vector<MegaNode *> *nodesMatching)
{
    if (!pathParts.size())
    {
        return;
    }

    string currentPart = pathParts.front();
    pathParts.pop();

    if (currentPart == ".")
    {
        getNodesMatching(parentNode, pathParts, nodesMatching);
    }

    MegaNodeList* children = api->getChildren(parentNode);
    if (children)
    {
        for (int i = 0; i < children->size(); i++)
        {
            MegaNode *childNode = children->get(i);
            if (patternMatches(childNode->getName(), currentPart.c_str()))
            {
                if (pathParts.size() == 0) //last leave
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
    do
    {
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

                if (( *ptr == '/' ) || ( *ptr == ':' ) || !*ptr)
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
            else if (( *ptr & 0xf0 ) == 0xe0)
            {
                l = 1;
            }
            else if (( *ptr & 0xf8 ) == 0xf0)
            {
                l = 2;
            }
            else if (( *ptr & 0xfc ) == 0xf8)
            {
                l = 3;
            }
            else if (( *ptr & 0xfe ) == 0xfc)
            {
                l = 4;
            }
        }
        else
        {
            l--;
        }
    }
    while (*ptr++);

    if (l)
    {
        return NULL;
    }

    if (remote)
    {
        // target: user inbox - record username/email and return NULL
        if (( c.size() == 2 ) && !c.back().size())
        {
            if (user)
            {
                *user = c.front();
            }

            return NULL;
        }
        MegaUserList * usersList = api->getContacts();
        MegaUser *u = NULL;
        for (int i = 0; i < usersList->size(); i++)
        {
            if (usersList->get(i)->getEmail() == c.front())
            {
                u = usersList->get(i);
                c.pop();
                break;
            }
        }

        if (u)
        {
            MegaNodeList* inshares = api->getInShares(u);
            for (int i = 0; i < inshares->size(); i++)
            {
                if (inshares->get(i)->getName() == c.front())
                {
                    n = inshares->get(i)->copy();
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
        if (( c.size() > 1 ) && !c.front().size())
        {
            c.pop();
            // path starting with //
            if (( c.size() > 1 ) && !c.front().size())
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

/**
 * @brief MegaCmdExecuter::nodesbypath
 * returns nodes determined by path pattern
 * path naming conventions:
 * path is relative to cwd
 * /path is relative to ROOT
 * //in is in INBOX
 * //bin is in RUBBISH
 * X: is user X's INBOX
 * X:SHARE is share SHARE from user X
 * : and / filename components, as well as the \, must be escaped by \.
 * (correct UTF-8 encoding is assumed)
 * @param ptr
 * @param user
 * @param namepart
 * @return List of MegaNode*.  You take the ownership of those MegaNode*
 */
vector <MegaNode*> * MegaCmdExecuter::nodesbypath(const char* ptr, string* user, string* namepart)
{
    vector<MegaNode *> *nodesMatching = new vector<MegaNode *> ();
    queue<string> c;
    string s;
    int l = 0;
    const char* bptr = ptr;
    int remote = 0;
    MegaNode* n;

    // split path by / or :
    do
    {
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

                if (( *ptr == '/' ) || ( *ptr == ':' ) || !*ptr)
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

                    if (s.size())
                    {
                        c.push(s);
                    }

                    s.erase();
                }
            }
            else if (( *ptr & 0xf0 ) == 0xe0)
            {
                l = 1;
            }
            else if (( *ptr & 0xf8 ) == 0xf0)
            {
                l = 2;
            }
            else if (( *ptr & 0xfc ) == 0xf8)
            {
                l = 3;
            }
            else if (( *ptr & 0xfe ) == 0xfc)
            {
                l = 4;
            }
        }
        else
        {
            l--;
        }
    }
    while (*ptr++);

    if (l)
    {
        return NULL;
    }

    if (remote)
    {
        // target: user inbox - record username/email and return NULL
        if (( c.size() == 2 ) && !c.back().size())
        {
            if (user)
            {
                *user = c.front();
            }

            return NULL;
        }

        MegaUserList * usersList = api->getContacts();
        MegaUser *u = NULL;
        for (int i = 0; i < usersList->size(); i++)
        {
            if (usersList->get(i)->getEmail() == c.front())
            {
                u = usersList->get(i);
                c.pop();
                break;
            }
        }

        if (u)
        {
            MegaNodeList* inshares = api->getInShares(u);
            for (int i = 0; i < inshares->size(); i++)
            {
                if (inshares->get(i)->getName() == c.front())
                {
                    n = inshares->get(i)->copy();
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
        if (( c.size() > 1 ) && !c.front().size())
        {
            c.pop();
            // path starting with //
            if (( c.size() > 1 ) && !c.front().size())
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
 *  // a new transfer was added
 *  void DemoApp::transfer_added(Transfer* t)
 *  {
 *  }
 *
 *  // a queued transfer was removed
 *  void DemoApp::transfer_removed(Transfer* t)
 *  {
 *   displaytransferdetails(t, "removed\n");
 *  }
 *
 *  void DemoApp::transfer_update(Transfer* t)
 *  {
 *   // (this is handled in the prompt logic)
 *  }
 *
 *  void DemoApp::transfer_failed(Transfer* t, error e)
 *  {
 *   displaytransferdetails(t, "failed (");
 *   OUTSTREAM << errorstring(e) << ")" << endl;
 *  }
 *
 *  void DemoApp::transfer_limit(Transfer *t)
 *  {
 *   displaytransferdetails(t, "bandwidth limit reached\n");
 *  }
 *
 *  void DemoApp::transfer_complete(Transfer* t)
 *  {
 *   displaytransferdetails(t, "completed, ");
 *
 *   if (t->slot)
 *   {
 *       OUTSTREAM << t->slot->progressreported * 10 / (1024 * (Waiter::ds - t->slot->starttime + 1)) << " KB/s" << endl;
 *   }
 *   else
 *   {
 *       OUTSTREAM << "delayed" << endl;
 *   }
 *  }
 *
 *  // transfer about to start - make final preparations (determine localfilename, create thumbnail for image upload)
 *  void DemoApp::transfer_prepare(Transfer* t)
 *  {
 *   displaytransferdetails(t, "starting\n");
 *
 *   if (t->type == GET)
 *   {
 *       // only set localfilename if the engine has not already done so
 *       if (!t->localfilename.size())
 *       {
 *           client->fsaccess->tmpnamelocal(&t->localfilename);
 *       }
 *   }
 *  }
 *
 * #ifdef ENABLE_SYNC
 *  static void MegaCmdExecuter::syncstat(Sync* sync)
 *  {
 *   OUTSTREAM << ", local data in this sync: " << sync->localbytes << " byte(s) in " << sync->localnodes[FILENODE]
 *        << " file(s) and " << sync->localnodes[FOLDERNODE] << " folder(s)" << endl;
 *  }
 *
 *  void DemoApp::syncupdate_state(Sync*, syncstate_t newstate)
 *  {
 *   switch (newstate)
 *   {
 *       case SYNC_ACTIVE:
 *           OUTSTREAM << "Sync is now active" << endl;
 *           break;
 *
 *       case SYNC_FAILED:
 *           OUTSTREAM << "Sync failed." << endl;
 *
 *       default:
 *           ;
 *   }
 *  }
 *
 *  void DemoApp::syncupdate_scanning(bool active)
 *  {
 *   if (active)
 *   {
 *       OUTSTREAM << "Sync - scanning files and folders" << endl;
 *   }
 *   else
 *   {
 *       OUTSTREAM << "Sync - scan completed" << endl;
 *   }
 *  }
 *
 *  // sync update callbacks are for informational purposes only and must not change or delete the sync itself
 *  void DemoApp::syncupdate_local_folder_addition(Sync* sync, LocalNode *, const char* path)
 *  {
 *   OUTSTREAM << "Sync - local folder addition detected: " << path;
 *   syncstat(sync);
 *  }
 *
 *  void DemoApp::syncupdate_local_folder_deletion(Sync* sync, LocalNode *localNode)
 *  {
 *   OUTSTREAM << "Sync - local folder deletion detected: " << localNode->name;
 *   syncstat(sync);
 *  }
 *
 *  void DemoApp::syncupdate_local_file_addition(Sync* sync, LocalNode *, const char* path)
 *  {
 *   OUTSTREAM << "Sync - local file addition detected: " << path;
 *   syncstat(sync);
 *  }
 *
 *  void DemoApp::syncupdate_local_file_deletion(Sync* sync, LocalNode *localNode)
 *  {
 *   OUTSTREAM << "Sync - local file deletion detected: " << localNode->name;
 *   syncstat(sync);
 *  }
 *
 *  void DemoApp::syncupdate_local_file_change(Sync* sync, LocalNode *, const char* path)
 *  {
 *   OUTSTREAM << "Sync - local file change detected: " << path;
 *   syncstat(sync);
 *  }
 *
 *  void DemoApp::syncupdate_local_move(Sync*, LocalNode *localNode, const char* path)
 *  {
 *   OUTSTREAM << "Sync - local rename/move " << localNode->name << " -> " << path << endl;
 *  }
 *
 *  void DemoApp::syncupdate_local_lockretry(bool locked)
 *  {
 *   if (locked)
 *   {
 *       OUTSTREAM << "Sync - waiting for local filesystem lock" << endl;
 *   }
 *   else
 *   {
 *       OUTSTREAM << "Sync - local filesystem lock issue resolved, continuing..." << endl;
 *   }
 *  }
 *
 *  void DemoApp::syncupdate_remote_move(Sync *, Node *n, Node *prevparent)
 *  {
 *   OUTSTREAM << "Sync - remote move " << n->displayname() << ": " << (prevparent ? prevparent->displayname() : "?") <<
 *           " -> " << (n->parent ? n->parent->displayname() : "?") << endl;
 *  }
 *
 *  void DemoApp::syncupdate_remote_rename(Sync *, Node *n, const char *prevname)
 *  {
 *   OUTSTREAM << "Sync - remote rename " << prevname << " -> " <<  n->displayname() << endl;
 *  }
 *
 *  void DemoApp::syncupdate_remote_folder_addition(Sync *, Node* n)
 *  {
 *   OUTSTREAM << "Sync - remote folder addition detected " << n->displayname() << endl;
 *  }
 *
 *  void DemoApp::syncupdate_remote_file_addition(Sync *, Node* n)
 *  {
 *   OUTSTREAM << "Sync - remote file addition detected " << n->displayname() << endl;
 *  }
 *
 *  void DemoApp::syncupdate_remote_folder_deletion(Sync *, Node* n)
 *  {
 *   OUTSTREAM << "Sync - remote folder deletion detected " << n->displayname() << endl;
 *  }
 *
 *  void DemoApp::syncupdate_remote_file_deletion(Sync *, Node* n)
 *  {
 *   OUTSTREAM << "Sync - remote file deletion detected " << n->displayname() << endl;
 *  }
 *
 *  void DemoApp::syncupdate_get(Sync*, Node *, const char* path)
 *  {
 *   OUTSTREAM << "Sync - requesting file " << path << endl;
 *  }
 *
 *  void DemoApp::syncupdate_put(Sync*, LocalNode *, const char* path)
 *  {
 *   OUTSTREAM << "Sync - sending file " << path << endl;
 *  }
 *
 *  void DemoApp::syncupdate_remote_copy(Sync*, const char* name)
 *  {
 *   OUTSTREAM << "Sync - creating remote file " << name << " by copying existing remote file" << endl;
 *  }
 *
 *  static const char* treestatename(treestate_t ts)
 *  {
 *   switch (ts)
 *   {
 *       case TREESTATE_NONE:
 *           return "None/Undefined";
 *       case TREESTATE_SYNCED:
 *           return "Synced";
 *       case TREESTATE_PENDING:
 *           return "Pending";
 *       case TREESTATE_SYNCING:
 *           return "Syncing";
 *   }
 *
 *   return "UNKNOWN";
 *  }
 *
 *  void DemoApp::syncupdate_treestate(LocalNode* l)
 *  {
 *   OUTSTREAM << "Sync - state change of node " << l->name << " to " << treestatename(l->ts) << endl;
 *  }
 *
 *  // generic name filter
 *  // FIXME: configurable regexps
 *  static bool is_syncable(const char* name)
 *  {
 *   return *name != '.' && *name != '~' && strcmp(name, "Thumbs.db") && strcmp(name, "desktop.ini");
 *  }
 *
 *  // determines whether remote node should be synced
 *  bool DemoApp::sync_syncable(Node* n)
 *  {
 *   return is_syncable(n->displayname());
 *  }
 *
 *  // determines whether local file should be synced
 *  bool DemoApp::sync_syncable(const char* name, string* localpath, string* localname)
 *  {
 *   return is_syncable(name);
 *  }
 * #endif
 *
 *  //AppFileGet::AppFileGet(Node* n, handle ch, byte* cfilekey, m_off_t csize, m_time_t cmtime, string* cfilename,
 *  //                       string* cfingerprint)
 *  //{
 *  //    if (n)
 *  //    {
 *  //        h = n->nodehandle;
 *  //        hprivate = true;
 *
 *  //        *(FileFingerprint*) this = *n;
 *  //        name = n->displayname();
 *  //    }
 *  //    else
 *  //    {
 *  //        h = ch;
 *  //        memcpy(filekey, cfilekey, sizeof filekey);
 *  //        hprivate = false;
 *
 *  //        size = csize;
 *  //        mtime = cmtime;
 *
 *  //        if (!cfingerprint->size() || !unserializefingerprint(cfingerprint))
 *  //        {
 *  //            memcpy(crc, filekey, sizeof crc);
 *  //        }
 *
 *  //        name = *cfilename;
 *  //    }
 *
 *  //    localname = name;
 *  //    client->fsaccess->name2local(&localname);
 *  //}
 *
 *  //AppFilePut::AppFilePut(string* clocalname, handle ch, const char* ctargetuser)
 *  //{
 *  //    // this assumes that the local OS uses an ASCII path separator, which should be true for most
 *  //    string separator = client->fsaccess->localseparator;
 *
 *  //    // full local path
 *  //    localname = *clocalname;
 *
 *  //    // target parent node
 *  //    h = ch;
 *
 *  //    // target user
 *  //    targetuser = ctargetuser;
 *
 *  //    // erase path component
 *  //    name = *clocalname;
 *  //    client->fsaccess->local2name(&name);
 *  //    client->fsaccess->local2name(&separator);
 *
 *  //    name.erase(0, name.find_last_of(*separator.c_str()) + 1);
 *  //}
 *
 *  // user addition/update (users never get deleted)
 *  void DemoApp::users_updated(User** u, int count)
 *  {
 *   if (count == 1)
 *   {
 *       OUTSTREAM << "1 user received or updated" << endl;
 *   }
 *   else
 *   {
 *       OUTSTREAM << count << " users received or updated" << endl;
 *   }
 *  }
 *
 * #ifdef ENABLE_CHAT
 *
 *  void DemoApp::chatcreate_result(TextChat *chat, error e)
 *  {
 *   if (e)
 *   {
 *       OUTSTREAM << "Chat creation failed (" << errorstring(e) << ")" << endl;
 *   }
 *   else
 *   {
 *       OUTSTREAM << "Chat created successfully" << endl;
 *       printChatInformation(chat);
 *       OUTSTREAM << endl;
 *   }
 *  }
 *
 *  void DemoApp::chatfetch_result(textchat_vector *chats, error e)
 *  {
 *   if (e)
 *   {
 *       OUTSTREAM << "Chat fetching failed (" << errorstring(e) << ")" << endl;
 *   }
 *   else
 *   {
 *       if (chats->size() == 1)
 *       {
 *           OUTSTREAM << "1 chat received or updated" << endl;
 *       }
 *       else
 *       {
 *           OUTSTREAM << chats->size() << " chats received or updated" << endl;
 *       }
 *
 *       for (textchat_vector::iterator it = chats->begin(); it < chats->end(); it++)
 *       {
 *           printChatInformation(*it);
 *           OUTSTREAM << endl;
 *       }
 *   }
 *  }
 *
 *  void DemoApp::chatinvite_result(error e)
 *  {
 *   if (e)
 *   {
 *       OUTSTREAM << "Chat invitation failed (" << errorstring(e) << ")" << endl;
 *   }
 *   else
 *   {
 *       OUTSTREAM << "Chat invitation successful" << endl;
 *   }
 *  }
 *
 *  void DemoApp::chatremove_result(error e)
 *  {
 *   if (e)
 *   {
 *       OUTSTREAM << "Peer removal failed (" << errorstring(e) << ")" << endl;
 *   }
 *   else
 *   {
 *       OUTSTREAM << "Peer removal successful" << endl;
 *   }
 *  }
 *
 *  void DemoApp::chaturl_result(string *url, error e)
 *  {
 *   if (e)
 *   {
 *       OUTSTREAM << "Chat URL retrieval failed (" << errorstring(e) << ")" << endl;
 *   }
 *   else
 *   {
 *       OUTSTREAM << "Chat URL: " << *url << endl;
 *   }
 *
 *  }
 *
 *  void DemoApp::chatgrantaccess_result(error e)
 *  {
 *   if (e)
 *   {
 *       OUTSTREAM << "Grant access to node failed (" << errorstring(e) << ")" << endl;
 *   }
 *   else
 *   {
 *       OUTSTREAM << "Access to node granted successfully" << endl;
 *   }
 *  }
 *
 *  void DemoApp::chatremoveaccess_result(error e)
 *  {
 *   if (e)
 *   {
 *       OUTSTREAM << "Revoke access to node failed (" << errorstring(e) << ")" << endl;
 *   }
 *   else
 *   {
 *       OUTSTREAM << "Access to node removed successfully" << endl;
 *   }
 *  }
 *
 *  void DemoApp::chats_updated(textchat_vector *chats)
 *  {
 *   if (chats)
 *   {
 *       if (chats->size() == 1)
 *       {
 *           OUTSTREAM << "1 chat updated or created" << endl;
 *       }
 *       else
 *       {
 *           OUTSTREAM << chats->size() << " chats updated or created" << endl;
 *       }
 *   }
 *  }
 *
 *  void DemoApp::printChatInformation(TextChat *chat)
 *  {
 *   if (!chat)
 *   {
 *       return;
 *   }
 *
 *   char hstr[sizeof(handle) * 4 / 3 + 4];
 *   Base64::btoa((const byte *)&chat->id, sizeof(handle), hstr);
 *
 *   OUTSTREAM << "Chat ID: " << hstr << endl;
 *   OUTSTREAM << "\tOwn privilege level: " << getPrivilegeString(chat->priv) << endl;
 *   OUTSTREAM << "\tChat shard: " << chat->shard << endl;
 *   OUTSTREAM << "\tURL: " << chat->url << endl;
 *   if (chat->group)
 *   {
 *       OUTSTREAM << "\tGroup chat: yes" << endl;
 *   }
 *   else
 *   {
 *       OUTSTREAM << "\tGroup chat: no" << endl;
 *   }
 *   OUTSTREAM << "\tPeers:";
 *
 *   if (chat->userpriv)
 *   {
 *       OUTSTREAM << "\t\t(userhandle)\t(privilege level)" << endl;
 *       for (unsigned i = 0; i < chat->userpriv->size(); i++)
 *       {
 *           Base64::btoa((const byte *)&chat->userpriv->at(i).first, sizeof(handle), hstr);
 *           OUTSTREAM << "\t\t\t" << hstr;
 *           OUTSTREAM << "\t" << getPrivilegeString(chat->userpriv->at(i).second) << endl;
 *       }
 *   }
 *   else
 *   {
 *       OUTSTREAM << " no peers (only you as participant)" << endl;
 *   }
 *  }
 *
 *  string DemoApp::getPrivilegeString(privilege_t priv)
 *  {
 *   switch (priv)
 *   {
 *   case PRIV_FULL:
 *       return "PRIV_FULL (full access)";
 *   case PRIV_OPERATOR:
 *       return "PRIV_OPERATOR (operator)";
 *   case PRIV_RO:
 *       return "PRIV_RO (read-only)";
 *   case PRIV_RW:
 *       return "PRIV_RW (read-write)";
 *   case PRIV_RM:
 *       return "PRIV_RM (removed)";
 *   case PRIV_UNKNOWN:
 *   default:
 *       return "PRIV_UNKNOWN";
 *   }
 *  }
 *
 * #endif
 *
 *
 *  void DemoApp::pcrs_updated(PendingContactRequest** list, int count)
 *  {
 *   int deletecount = 0;
 *   int updatecount = 0;
 *   if (list != NULL)
 *   {
 *       for (int i = 0; i < count; i++)
 *       {
 *           if (list[i]->changed.deleted)
 *           {
 *               deletecount++;
 *           }
 *           else
 *           {
 *               updatecount++;
 *           }
 *       }
 *   }
 *   else
 *   {
 *       // All pcrs are updated
 *       for (handlepcr_map::iterator it = client->pcrindex.begin(); it != client->pcrindex.end(); it++)
 *       {
 *           if (it->second->changed.deleted)
 *           {
 *               deletecount++;
 *           }
 *           else
 *           {
 *               updatecount++;
 *           }
 *       }
 *   }
 *
 *   if (deletecount != 0)
 *   {
 *       OUTSTREAM << deletecount << " pending contact request" << (deletecount != 1 ? "s" : "") << " deleted" << endl;
 *   }
 *   if (updatecount != 0)
 *   {
 *       OUTSTREAM << updatecount << " pending contact request" << (updatecount != 1 ? "s" : "") << " received or updated" << endl;
 *   }
 *  }
 *
 *  void DemoApp::setattr_result(handle, error e)
 *  {
 *   if (e)
 *   {
 *       OUTSTREAM << "Node attribute update failed (" << errorstring(e) << ")" << endl;
 *   }
 *  }
 *
 *  void DemoApp::rename_result(handle, error e)
 *  {
 *   if (e)
 *   {
 *       OUTSTREAM << "Node move failed (" << errorstring(e) << ")" << endl;
 *   }
 *  }
 *
 *  void DemoApp::unlink_result(handle, error e)
 *  {
 *   if (e)
 *   {
 *       OUTSTREAM << "Node deletion failed (" << errorstring(e) << ")" << endl;
 *   }
 *  }
 *
 *  void DemoApp::fetchnodes_result(error e)
 *  {
 *   if (e)
 *   {
 *       OUTSTREAM << "File/folder retrieval failed (" << errorstring(e) << ")" << endl;
 *   }
 *   else
 *   {
 *       // check if we fetched a folder link and the key is invalid
 *       handle h = client->getrootpublicfolder();
 *       if (h != UNDEF)
 *       {
 *           Node *n = client->nodebyhandle(h);
 *           if (n && (n->attrs.map.find('n') == n->attrs.map.end()))
 *           {
 *               OUTSTREAM << "File/folder retrieval succeed, but encryption key is wrong." << endl;
 *           }
 *       }
 *   }
 *  }
 *
 *  void DemoApp::putnodes_result(error e, targettype_t t, NewNode* nn)
 *  {
 *   if (t == USER_HANDLE)
 *   {
 *       delete[] nn;
 *
 *       if (!e)
 *       {
 *           OUTSTREAM << "Success." << endl;
 *       }
 *   }
 *
 *   if (e)
 *   {
 *       OUTSTREAM << "Node addition failed (" << errorstring(e) << ")" << endl;
 *   }
 *  }
 *
 *  void DemoApp::share_result(error e)
 *  {
 *   if (e)
 *   {
 *       OUTSTREAM << "Share creation/modification request failed (" << errorstring(e) << ")" << endl;
 *   }
 *  }
 *
 *  void DemoApp::share_result(int, error e)
 *  {
 *   if (e)
 *   {
 *       OUTSTREAM << "Share creation/modification failed (" << errorstring(e) << ")" << endl;
 *   }
 *   else
 *   {
 *       OUTSTREAM << "Share creation/modification succeeded" << endl;
 *   }
 *  }
 *
 *  void DemoApp::setpcr_result(handle h, error e, opcactions_t action)
 *  {
 *   if (e)
 *   {
 *       OUTSTREAM << "Outgoing pending contact request failed (" << errorstring(e) << ")" << endl;
 *   }
 *   else
 *   {
 *       if (h == UNDEF)
 *       {
 *           // must have been deleted
 *           OUTSTREAM << "Outgoing pending contact request " << (action == OPCA_DELETE ? "deleted" : "reminded") << " successfully" << endl;
 *       }
 *       else
 *       {
 *           char buffer[12];
 *           Base64::btoa((byte*)&h, sizeof(h), buffer);
 *           OUTSTREAM << "Outgoing pending contact request succeeded, id: " << buffer << endl;
 *       }
 *   }
 *  }
 *
 *  void DemoApp::updatepcr_result(error e, ipcactions_t action)
 *  {
 *   if (e)
 *   {
 *       OUTSTREAM << "Incoming pending contact request update failed (" << errorstring(e) << ")" << endl;
 *   }
 *   else
 *   {
 *       string labels[3] = {"accepted", "denied", "ignored"};
 *       OUTSTREAM << "Incoming pending contact request successfully " << labels[(int)action] << endl;
 *   }
 *  }
 *
 *  void DemoApp::fa_complete(Node* n, fatype type, const char* data, uint32_t len)
 *  {
 *   OUTSTREAM << "Got attribute of type " << type << " (" << len << " byte(s)) for " << n->displayname() << endl;
 *  }
 *
 *  int DemoApp::fa_failed(handle, fatype type, int retries, error e)
 *  {
 *   OUTSTREAM << "File attribute retrieval of type " << type << " failed (retries: " << retries << ") error: " << e << endl;
 *
 *   return retries > 2;
 *  }
 *
 *  void DemoApp::putfa_result(handle, fatype, error e)
 *  {
 *   if (e)
 *   {
 *       OUTSTREAM << "File attribute attachment failed (" << errorstring(e) << ")" << endl;
 *   }
 *  }
 *
 *  void DemoApp::invite_result(error e)
 *  {
 *   if (e)
 *   {
 *       OUTSTREAM << "Invitation failed (" << errorstring(e) << ")" << endl;
 *   }
 *   else
 *   {
 *       OUTSTREAM << "Success." << endl;
 *   }
 *  }
 *
 *  void DemoApp::putua_result(error e)
 *  {
 *   if (e)
 *   {
 *       OUTSTREAM << "User attribute update failed (" << errorstring(e) << ")" << endl;
 *   }
 *   else
 *   {
 *       OUTSTREAM << "Success." << endl;
 *   }
 *  }
 *
 *  void DemoApp::getua_result(error e)
 *  {
 *   OUTSTREAM << "User attribute retrieval failed (" << errorstring(e) << ")" << endl;
 *  }
 *
 *  void DemoApp::getua_result(byte* data, unsigned l)
 *  {
 *   OUTSTREAM << "Received " << l << " byte(s) of user attribute: ";
 *   fwrite(data, 1, l, stdout);
 *   OUTSTREAM << endl;
 *  }
 *
 *  void DemoApp::notify_retry(dstime dsdelta)
 *  {
 *   if (dsdelta)
 *   {
 *       OUTSTREAM << "API request failed, retrying in " << dsdelta * 100 << " ms - Use 'retry' to retry immediately..."
 *            << endl;
 *   }
 *   else
 *   {
 *       OUTSTREAM << "Retried API request completed" << endl;
 *   }
 *  }
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
    if (!title && !( title = n->getName()))
    {
        title = "CRYPTO_ERROR";
    }

    if (depth)
    {
        for (int i = depth - 1; i--; )
        {
            OUTSTREAM << "\t";
        }
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
                if (( p = strchr(n->getAttrString()->c_str(), ':')))
                {
                    OUTSTREAM << ", has attributes " << p + 1;
                }

                if (INVALID_HANDLE != n->getPublicHandle())
//            if (n->isExported())
                {
                    OUTSTREAM << ", shared as exported";
                    if (n->getExpirationTime())
                    {
                        OUTSTREAM << " temporal";
                    }
                    else
                    {
                        OUTSTREAM << " permanent";
                    }
                    OUTSTREAM << " file link";
                    if (extended_info > 1)
                    {
                        char * publicLink = n->getPublicLink();
                        OUTSTREAM << ": " << publicLink;
                        if (n->getExpirationTime())
                        {
                            if (n->isExpired())
                            {
                                OUTSTREAM << " expired at ";
                            }
                            else
                            {
                                OUTSTREAM << " expires at ";
                            }
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
                    for (int i = 0; i < outShares->size(); i++)
                    {
                        if (outShares->get(i)->getNodeHandle() == n->getHandle())
                        {
                            OUTSTREAM << ", shared with " << outShares->get(i)->getUser() << ", access "
                                      << getAccessLevelStr(outShares->get(i)->getAccess());
                        }
                    }

                    MegaShareList* pendingoutShares = api->getPendingOutShares(n);
                    if (pendingoutShares)
                    {
                        for (int i = 0; i < pendingoutShares->size(); i++)
                        {
                            if (pendingoutShares->get(i)->getNodeHandle() == n->getHandle())
                            {
                                OUTSTREAM << ", shared (still pending)";
                                if (pendingoutShares->get(i)->getUser())
                                {
                                    OUTSTREAM << " with " << pendingoutShares->get(i)->getUser();
                                }
                                OUTSTREAM << " access " << getAccessLevelStr(pendingoutShares->get(i)->getAccess());
                            }
                        }

                        delete pendingoutShares;
                    }

                    if (UNDEF != n->getPublicHandle())
                    {
                        OUTSTREAM << ", shared as exported";
                        if (n->getExpirationTime())
                        {
                            OUTSTREAM << " temporal";
                        }
                        else
                        {
                            OUTSTREAM << " permanent";
                        }
                        OUTSTREAM << " folder link";
                        if (extended_info > 1)
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
                    OUTSTREAM << ", inbound " << api->getAccess(n) << " share";
                }
                break;
            }

            default:
                OUTSTREAM << "unsupported type, please upgrade";
        }
        OUTSTREAM << ")" << ( n->isRemoved() ? " (DELETED)" : "" );
    }

    OUTSTREAM << endl;
}

void MegaCmdExecuter::dumptree(MegaNode* n, int recurse, int extended_info, int depth, string pathRelativeTo)
{
    if (depth || ( n->getType() == MegaNode::TYPE_FILE ))
    {
        if (pathRelativeTo != "NULL")
        {
            if (!n->getName())
            {
                dumpNode(n, extended_info, depth, "CRYPTO_ERROR");
            }
            else
            {
                char * nodepath = api->getNodePath(n);


                char *pathToShow = NULL;
                if (pathRelativeTo != "")
                {
                    pathToShow = strstr(nodepath, pathRelativeTo.c_str());
                }

                if (pathToShow == nodepath)     //found at beginning
                {
                    pathToShow += pathRelativeTo.size();
                    if (( *pathToShow == '/' ) && ( pathRelativeTo != "/" ))
                    {
                        pathToShow++;
                    }
                }
                else
                {
                    pathToShow = nodepath;
                }

                dumpNode(n, extended_info, depth, pathToShow);

                delete nodepath;
            }
        }
        else
        {
                dumpNode(n, extended_info, depth);
        }

        if (!recurse && depth)
        {
            return;
        }
    }

    if (n->getType() != MegaNode::TYPE_FILE)
    {
        MegaNodeList* children = api->getChildren(n);
        if (children)
        {
            for (int i = 0; i < children->size(); i++)
            {
                dumptree(children->get(i), recurse, extended_info, depth + 1);
            }

            delete children;
        }
    }
}

MegaContactRequest * MegaCmdExecuter::getPcrByContact(string contactEmail)
{
    MegaContactRequestList *icrl = api->getIncomingContactRequests();
    if (icrl)
    {
        for (int i = 0; i < icrl->size(); i++)
        {
            if (icrl->get(i)->getSourceEmail() == contactEmail)
            {
                return icrl->get(i);
                delete icrl;
            }
        }
        delete icrl;
    }
    return NULL;
}

string MegaCmdExecuter::getDisplayPath(string givenPath, MegaNode* n)
{
    char * pathToNode = api->getNodePath(n);
    char * pathToShow = pathToNode;

    string pathRelativeTo = "NULL";
    string cwpath = getCurrentPath();

    if (givenPath.find('/') == 0)
    {
        pathRelativeTo = "";
    }
    else
    {
        if (cwpath == "/")
        {
            pathRelativeTo = cwpath;
        }
        else
        {
            pathRelativeTo = cwpath + "/";
        }
    }

    if (( "" == givenPath ) && !strcmp(pathToNode, cwpath.c_str()))
    {
        pathToNode[0] = '.';
        pathToNode[1] = '\0';
    }

    if (pathRelativeTo != "")
    {
        pathToShow = strstr(pathToNode, pathRelativeTo.c_str());
    }

    if (pathToShow == pathToNode)     //found at beginning
    {
        pathToShow += pathRelativeTo.size();
    }
    else
    {
        pathToShow = pathToNode;
    }

    string toret(pathToShow);
    delete []pathToNode;
    return toret;
}

void MegaCmdExecuter::dumpListOfExported(MegaNode* n, string givenPath)
{
    vector<MegaNode *> listOfExported;
    processTree(n, includeIfIsExported, (void*)&listOfExported);
    for (std::vector< MegaNode * >::iterator it = listOfExported.begin(); it != listOfExported.end(); ++it)
    {
        MegaNode * n = *it;
        if (n)
        {
            string pathToShow = getDisplayPath(givenPath, n);
            dumpNode(n, 2, 1, pathToShow.c_str());

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
    MegaShareList* outShares = api->getOutShares(n);
    if (outShares)
    {
        for (int i = 0; i < outShares->size(); i++)
        {
            OUTSTREAM << name ? name : n->getName();

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
    processTree(n, includeIfIsShared, (void*)&listOfShared);
    for (std::vector< MegaNode * >::iterator it = listOfShared.begin(); it != listOfShared.end(); ++it)
    {
        MegaNode * n = *it;
        if (n)
        {
            string pathToShow = getDisplayPath(givenPath, n);
            //dumpNode(n, 3, 1,pathToShow.c_str());
            listnodeshares(n, pathToShow);

            delete n;
        }
    }

    listOfShared.clear();
}

//includes pending and normal shares
void MegaCmdExecuter::dumpListOfAllShared(MegaNode* n, string givenPath)
{
    vector<MegaNode *> listOfShared;
    processTree(n, includeIfIsSharedOrPendingOutShare, (void*)&listOfShared);
    for (std::vector< MegaNode * >::iterator it = listOfShared.begin(); it != listOfShared.end(); ++it)
    {
        MegaNode * n = *it;
        if (n)
        {
            string pathToShow = getDisplayPath(givenPath, n);
            dumpNode(n, 3, 1, pathToShow.c_str());
            //notice: some nodes may be dumped twice

            delete n;
        }
    }

    listOfShared.clear();
}

void MegaCmdExecuter::dumpListOfPendingShares(MegaNode* n, string givenPath)
{
    vector<MegaNode *> listOfShared;
    processTree(n, includeIfIsPendingOutShare, (void*)&listOfShared);

    for (std::vector< MegaNode * >::iterator it = listOfShared.begin(); it != listOfShared.end(); ++it)
    {
        MegaNode * n = *it;
        if (n)
        {
            string pathToShow = getDisplayPath(givenPath, n);
            dumpNode(n, 3, 1, pathToShow.c_str());

            delete n;
        }
    }

    listOfShared.clear();
}


void MegaCmdExecuter::loginWithPassword(char *password)
{
    MegaCmdListener *megaCmdListener = new MegaCmdListener(NULL);
    api->login(login.c_str(), password, megaCmdListener);
    actUponLogin(megaCmdListener);
    delete megaCmdListener;
}


void MegaCmdExecuter::changePassword(const char *oldpassword, const char *newpassword)
{
    MegaCmdListener *megaCmdListener = new MegaCmdListener(NULL);
    api->changePassword(oldpassword, newpassword, megaCmdListener);
    megaCmdListener->wait();
    if (!checkNoErrors(megaCmdListener->getError(), "change password"))
    {
        OUTSTREAM << "Please, ensure you enter the old password correctly" << endl;
    }
    else
    {
        OUTSTREAM << "Password changed succesfully" << endl;
    }
    delete megaCmdListener;
}

//appfile_list appxferq[2];

int MegaCmdExecuter::loadfile(string* name, string* data)
{
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

void MegaCmdExecuter::actUponGetExtendedAccountDetails(SynchronousRequestListener *srl, int timeout)
{
    if (timeout == -1)
    {
        srl->wait();
    }
    else
    {
        int trywaitout = srl->trywait(timeout);
        if (trywaitout)
        {
            LOG_err << "GetExtendedAccountDetails took too long, it may have failed. No further actions performed";
            return;
        }
    }

    if (checkNoErrors(srl->getError(), "failed to GetExtendedAccountDetails"))
    {
        char timebuf[32], timebuf2[32];

        LOG_verbose << "actUponGetExtendedAccountDetails ok";

        MegaAccountDetails *details = srl->getRequest()->getMegaAccountDetails();
        if (details)
        {
            OUTSTREAM << "\tAvailable storage: " << details->getStorageMax() << " byte(s)" << endl;
            MegaNode *n = api->getRootNode();
            OUTSTREAM << "\t\tIn ROOT: " << details->getStorageUsed(n->getHandle()) << " byte(s) in "
                      << details->getNumFiles(n->getHandle()) << " file(s) and " << details->getNumFolders(n->getHandle()) << " folder(s)" << endl;
            delete n;

            n = api->getInboxNode();
            OUTSTREAM << "\t\tIn INBOX: " << details->getStorageUsed(n->getHandle()) << " byte(s) in "
                      << details->getNumFiles(n->getHandle()) << " file(s) and " << details->getNumFolders(n->getHandle()) << " folder(s)" << endl;
            delete n;

            n = api->getRubbishNode();
            OUTSTREAM << "\t\tIn RUBBISH: " << details->getStorageUsed(n->getHandle()) << " byte(s) in "
                      << details->getNumFiles(n->getHandle()) << " file(s) and " << details->getNumFolders(n->getHandle()) << " folder(s)" << endl;
            delete n;


            MegaNodeList *inshares = api->getInShares();
            if (inshares)
            {
                for (int i = 0; i < inshares->size(); i++)
                {
                    n = inshares->get(i);
                    OUTSTREAM << "\t\tIn INSHARE " << n->getName() << ": " << details->getStorageUsed(n->getHandle()) << " byte(s) in "
                              << details->getNumFiles(n->getHandle()) << " file(s) and " << details->getNumFolders(n->getHandle()) << " folder(s)" << endl;
                }
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
                    OUTSTREAM << "\t\t"  << "Pro expiration date: " << timebuf << endl;
                }
            }
            char * subscriptionMethod = details->getSubscriptionMethod();
            OUTSTREAM << "\tSubscription type: " << subscriptionMethod << endl;
            delete []subscriptionMethod;
            OUTSTREAM << "\tAccount balance:" << endl;
            for (int i = 0; i < details->getNumBalances(); i++)
            {
                MegaAccountBalance * balance = details->getBalance(i);
                char sbalance[50];
                sprintf(sbalance, "\tBalance: %.3s %.02f", balance->getCurrency(), balance->getAmount());
                OUTSTREAM << "\t"  << "Balance: " << sbalance << endl;
            }

            if (details->getNumPurchases())
            {
                OUTSTREAM << "Purchase history:" << endl;
                for (int i = 0; i < details->getNumPurchases(); i++)
                {
                    MegaAccountPurchase *purchase = details->getPurchase(i);

                    time_t ts = purchase->getTimestamp();
                    char spurchase[150];

                    strftime(timebuf, sizeof timebuf, "%c", localtime(&ts));
                    sprintf(spurchase,"ID: %.11s Time: %s Amount: %.3s %.02f Payment method: %d\n",
                        purchase->getHandle(), timebuf, purchase->getCurrency(), purchase->getAmount(), purchase->getMethod());
                    OUTSTREAM << "\t"  << spurchase << endl;
                }
            }

            if (details->getNumTransactions())
            {
                OUTSTREAM << "Transaction history:" << endl;
                for (int i = 0; i < details->getNumTransactions(); i++)
                {
                    MegaAccountTransaction *transaction = details->getTransaction(i);
                    time_t ts = transaction->getTimestamp();
                    char stransaction[100];
                    strftime(timebuf, sizeof timebuf, "%c", localtime(&ts));
                    sprintf(stransaction, "ID: %.11s Time: %s Amount: %.3s %.02f\n",
                        transaction->getHandle(), timebuf, transaction->getCurrency(), transaction->getAmount());
                    OUTSTREAM << "\t"  << stransaction << endl;

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
                    strftime(timebuf,  sizeof timebuf, "%c", localtime(&ts));
                    ts = session->getMostRecentUsage();
                    strftime(timebuf2, sizeof timebuf, "%c", localtime(&ts));

                    char *sid = api->userHandleToBase64(session->getHandle());

                    if (session->isCurrent())
                    {
                        sprintf(sdetails, "\t* Current Session\n");
                    }
                    else
                    {
                        sprintf(sdetails, "");
                    }
                    char * userAgent = session->getUserAgent();
                    char * country = session->getCountry();
                    char * ip = session->getIP();

                    sprintf(sdetails, "%s\tSession ID: %s\n\tSession start: %s\n\tMost recent activity: %s\n\tIP: %s\n\tCountry: %.2s\n\tUser-Agent: %s\n\t-----\n",
                    sdetails,
                    sid,
                    timebuf,
                    timebuf2,
                    ip,
                    country,
                    userAgent
                    );
                    OUTSTREAM << sdetails;
                    delete []sid;
                    delete []userAgent;
                    delete []country;
                    delete []ip;
                    alive_sessions++;
                }
                delete session;
            }

            if (alive_sessions)
            {
                OUTSTREAM << details->getNumSessions() << " active sessions opened" << endl;
            }
            delete details;
        }
    }
}

bool MegaCmdExecuter::actUponFetchNodes(MegaApi *api, SynchronousRequestListener *srl, int timeout)
{
    if (timeout == -1)
    {
        srl->wait();
    }
    else
    {
        int trywaitout = srl->trywait(timeout);
        if (trywaitout)
        {
            LOG_err << "Fetch nodes took too long, it may have failed. No further actions performed";
            return false;
        }
    }

    if (checkNoErrors(srl->getError(), "fetch nodes"))
    {
        LOG_verbose << "actUponFetchNodes ok";

        MegaNode *cwdNode = ( cwd == UNDEF ) ? NULL : api->getNodeByHandle(cwd);
        if (( cwd == UNDEF ) || !cwdNode)
        {
            MegaNode *rootNode = srl->getApi()->getRootNode();
            cwd = rootNode->getHandle();
            delete rootNode;
        }
        if (cwdNode)
        {
            delete cwdNode;
        }
        updateprompt(api, cwd);
        LOG_debug << " Fetch nodes correctly";
        return true;
    }
    return false;
}

void MegaCmdExecuter::actUponLogin(SynchronousRequestListener *srl, int timeout)
{
    if (timeout == -1)
    {
        srl->wait();
    }
    else
    {
        int trywaitout = srl->trywait(timeout);
        if (trywaitout)
        {
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
    else if (checkNoErrors(srl->getError(), "Login")) //login success:
    {
        LOG_info << "Login correct ... " << srl->getRequest()->getEmail();
        session = srl->getApi()->dumpSession();
        ConfigurationManager::saveSession(session);
        LOG_info << "Fetching nodes ... ";
        srl->getApi()->fetchNodes(srl);
        actUponFetchNodes(api, srl, timeout);
    }
}

void MegaCmdExecuter::actUponLogout(SynchronousRequestListener *srl, bool deletedSession, int timeout)
{
    if (!timeout)
    {
        srl->wait();
    }
    else
    {
        int trywaitout = srl->trywait(timeout);
        if (trywaitout)
        {
            LOG_err << "Logout took too long, it may have failed. No further actions performed";
            return;
        }
    }
    if (checkNoErrors(srl->getError(), "logout"))
    {
        LOG_verbose << "actUponLogout logout ok";
        cwd = UNDEF;
        delete []session;
        session = NULL;
        if (deletedSession)
            ConfigurationManager::saveSession("");
    }
    updateprompt(api, cwd);
}

int MegaCmdExecuter::actUponCreateFolder(SynchronousRequestListener *srl, int timeout)
{
    if (!timeout)
    {
        srl->wait();
    }
    else
    {
        int trywaitout = srl->trywait(timeout);
        if (trywaitout)
        {
            LOG_err << "actUponCreateFolder took too long, it may have failed. No further actions performed";
            return 1;
        }
    }
    if (checkNoErrors(srl->getError(), "create folder"))
    {
        LOG_verbose << "actUponCreateFolder Create Folder ok";
        return 0;
    }

    return 2;
}

void MegaCmdExecuter::deleteNode(MegaNode *nodeToDelete, MegaApi* api, int recursive)
{
    char *nodePath = api->getNodePath(nodeToDelete);
    if (( nodeToDelete->getType() == MegaNode::TYPE_FOLDER ) && !recursive)
    {
        setCurrentOutCode(5);
        OUTSTREAM << "Unable to delete folder: " << nodePath << ". Use -r to delete a folder recursively" << endl;
    }
    else
    {
        LOG_verbose << "Deleting: " << nodePath;
        MegaCmdListener *megaCmdListener = new MegaCmdListener(NULL);
        api->remove(nodeToDelete, megaCmdListener);
        megaCmdListener->wait();
        string msj = "delete node ";
        msj += nodePath;
        checkNoErrors(megaCmdListener->getError(), msj);
        delete megaCmdListener;
    }
    delete []nodePath;
}

void MegaCmdExecuter::downloadNode(string localPath, MegaApi* api, MegaNode *node)
{
    MegaCmdTransferListener *megaCmdTransferListener = new MegaCmdTransferListener(api, NULL);
    LOG_debug << "Starting download: " << node->getName() << " to : " << localPath;
    api->startDownload(node, localPath.c_str(), megaCmdTransferListener);
    megaCmdTransferListener->wait();
    if (checkNoErrors(megaCmdTransferListener->getError(), "download node"))
    {
        LOG_info << "Download complete: " << localPath << megaCmdTransferListener->getTransfer()->getFileName();
    }
    delete megaCmdTransferListener;
}

void MegaCmdExecuter::uploadNode(string localPath, MegaApi* api, MegaNode *node, string newname)
{
    MegaCmdTransferListener *megaCmdTransferListener = new MegaCmdTransferListener(api, NULL);
    LOG_debug << "Starting upload: " << localPath << " to : " << node->getName();
    if (newname.size())
    {
        api->startUpload(localPath.c_str(), node, newname.c_str(), megaCmdTransferListener);
    }
    else
    {
        api->startUpload(localPath.c_str(), node, megaCmdTransferListener);
    }
    megaCmdTransferListener->wait();
    if (checkNoErrors(megaCmdTransferListener->getError(), "Upload node"))
    {
        char * destinyPath = api->getNodePath(node);
        LOG_info << "Upload complete: " << localPath << " to " << destinyPath << newname;
        delete []destinyPath;
    }
    delete megaCmdTransferListener;
}


void MegaCmdExecuter::exportNode(MegaNode *n, int expireTime)
{
    MegaCmdListener *megaCmdListener = new MegaCmdListener(api, NULL);

    api->exportNode(n, expireTime, megaCmdListener);
    megaCmdListener->wait();
    if (checkNoErrors(megaCmdListener->getError(), "export node"))
    {
        MegaNode *nexported = api->getNodeByHandle(megaCmdListener->getRequest()->getNodeHandle());
        if (nexported)
        {
            char *nodepath = api->getNodePath(nexported);
            OUTSTREAM << "Exported " << nodepath << " : " << nexported->getPublicLink();
            if (nexported->getExpirationTime())
            {
                OUTSTREAM << " expires at " << getReadableTime(nexported->getExpirationTime());
            }
            OUTSTREAM << endl;
            delete[] nodepath;
            delete nexported;
        }
        else
        {
            setCurrentOutCode(2);
            LOG_err << "Exported node not found!";
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
    MegaCmdListener *megaCmdListener = new MegaCmdListener(api, NULL);

    api->disableExport(n, megaCmdListener);
    megaCmdListener->wait();
    if (checkNoErrors(megaCmdListener->getError(), "disable export"))
    {
        MegaNode *nexported = api->getNodeByHandle(megaCmdListener->getRequest()->getNodeHandle());
        if (nexported)
        {
            char *nodepath = api->getNodePath(nexported);
            OUTSTREAM << "Disabled export " << nodepath << " : " << nexported->getPublicLink() << endl;
            delete[] nodepath;
            delete nexported;
        }
        else
        {
            setCurrentOutCode(2);
            LOG_err << "Exported node not found!";
        }
    }

    delete megaCmdListener;
}

void MegaCmdExecuter::shareNode(MegaNode *n, string with, int level)
{
    MegaCmdListener *megaCmdListener = new MegaCmdListener(api, NULL);

    api->share(n, with.c_str(), level, megaCmdListener);
    megaCmdListener->wait();
    if (checkNoErrors(megaCmdListener->getError(), ( level != MegaShare::ACCESS_UNKNOWN ) ? "share node" : "disable share"))
    {
        MegaNode *nshared = api->getNodeByHandle(megaCmdListener->getRequest()->getNodeHandle());
        if (nshared)
        {
            char *nodepath = api->getNodePath(nshared);
            if (megaCmdListener->getRequest()->getAccess() == MegaShare::ACCESS_UNKNOWN)
            {
                OUTSTREAM << "Stopped sharing " << nodepath << " with " << megaCmdListener->getRequest()->getEmail() << endl;
            }
            else
            {
                OUTSTREAM << "Shared " << nodepath << " : " << megaCmdListener->getRequest()->getEmail()
                          << " accessLevel=" << megaCmdListener->getRequest()->getAccess() << endl;
            }
            delete[] nodepath;
            delete nshared;
        }
        else
        {
            setCurrentOutCode(2);
            LOG_err << "Shared node not found!";
        }
    }

    delete megaCmdListener;
}

void MegaCmdExecuter::disableShare(MegaNode *n, string with)
{
    shareNode(n, with, MegaShare::ACCESS_UNKNOWN);
}

string MegaCmdExecuter::getCurrentPath(){
    string toret;
    MegaNode *ncwd = api->getNodeByHandle(cwd);
    if (ncwd)
    {
        char *currentPath = api->getNodePath(ncwd);
        toret=string(currentPath);
        delete []currentPath;
        delete ncwd;
    }
    return toret;
}

vector<string> MegaCmdExecuter::listpaths(string askedPath)
{
    MegaNode *n;
    vector<string> paths;
    if ((int)askedPath.size())
    {
        string rNpath = "NULL";
        if (askedPath.find('/') != string::npos)
        {
            string cwpath = getCurrentPath();
            if (askedPath.find_first_of(cwpath) == 0)
            {
                rNpath = "";
            }
            else
            {
                rNpath = cwpath;
            }
        }

        if (isRegExp(askedPath))
        {
            vector<MegaNode *> *nodesToList = nodesbypath(askedPath.c_str());
            if (nodesToList)
            {
                for (std::vector< MegaNode * >::iterator it = nodesToList->begin(); it != nodesToList->end(); ++it)
                {
                    MegaNode * n = *it;
                    if (n)
                    {
                        string pathToShow = getDisplayPath(askedPath, n);
                        paths.push_back(pathToShow);
                        delete n;
                    }
                }

                nodesToList->clear();
                delete nodesToList;
            }
        }
        else
        {
            askedPath=unquote(askedPath);

            n = nodebypath(askedPath.c_str());
            if (n)
            {
                string pathToShow = getDisplayPath(askedPath, n);
//                dumptree(n, recursive, extended_info, 1,rNpath);
                delete n;
            }
        }
    }

    return paths;
}

vector<string> MegaCmdExecuter::getlistusers()
{
    vector<string> users;

    MegaUserList* usersList = api->getContacts();
    if (usersList)
    {
        for (int i = 0; i < usersList->size(); i++)
        {
            users.push_back(usersList->get(i)->getEmail());
        }

        delete usersList;
    }
    return users;
}

vector<string> MegaCmdExecuter::getNodeAttrs(string nodePath)
{
    vector<string> attrs;

    MegaNode *n = nodebypath(nodePath.c_str());
    if (n)
    {
        //List node custom attributes
        MegaStringList *attrlist = n->getCustomAttrNames();
        if (attrlist)
        {
            for (int a=0;a<attrlist->size();a++)
            {
                attrs.push_back(attrlist->get(a));
            }
            delete attrlist;
        }
        delete n;
    }
    return attrs;
}

vector<string> MegaCmdExecuter::getsessions()
{
    vector<string> sessions;
    MegaCmdListener *megaCmdListener = new MegaCmdListener(NULL);
    api->getExtendedAccountDetails(true, true, true, megaCmdListener);
    int trywaitout = megaCmdListener->trywait(3000);
    if (trywaitout)
    {
        return sessions;
    }

    if (checkNoErrors(megaCmdListener->getError(), "get sessions"))
    {
        MegaAccountDetails *details = megaCmdListener->getRequest()->getMegaAccountDetails();
        if (details)
        {
            int numSessions = details->getNumSessions();
            for (int i = 0; i < numSessions; i++)
            {
                MegaAccountSession * session = details->getSession(i);
                if (session)
                {
                    if (session->isAlive())
                    {
                        sessions.push_back(api->userHandleToBase64(session->getHandle()));
                    }
                    delete session;
                }
            }
            delete details;
        }
    }
    delete megaCmdListener;
    return sessions;
}


void MegaCmdExecuter::executecommand(vector<string> words, map<string, int> *clflags, map<string, string> *cloptions)
{
    MegaNode* n;

    if (words[0] == "ls")
    {
        int recursive = getFlag(clflags, "R") + getFlag(clflags, "r");
        int extended_info = getFlag(clflags, "l");

        if ((int)words.size() > 1)
        {
            string rNpath = "NULL";
            if (words[1].find('/') != string::npos)
            {
                string cwpath = getCurrentPath();
                if (words[1].find_first_of(cwpath) == 0)
                {
                    rNpath = "";
                }
                else
                {
                    rNpath = cwpath;
                }
            }

            if (isRegExp(words[1]))
            {
                vector<MegaNode *> *nodesToList = nodesbypath(words[1].c_str());
                if (nodesToList)
                {
                    for (std::vector< MegaNode * >::iterator it = nodesToList->begin(); it != nodesToList->end(); ++it)
                    {
                        MegaNode * n = *it;
                        if (n)
                        {
                            if (!n->getType() == MegaNode::TYPE_FILE)
                            {
                                OUTSTREAM << getDisplayPath(rNpath, n) << ": " << endl;
                            }
                            dumptree(n, recursive, extended_info, 0, rNpath);
                            if (( !n->getType() == MegaNode::TYPE_FILE ) && (( it + 1 ) != nodesToList->end()))
                            {
                                OUTSTREAM << endl;
                            }
                            delete n;
                        }
                    }

                    nodesToList->clear();
                    delete nodesToList;
                }
            }
            else
            {
                words[1]=unquote(words[1]);
                n = nodebypath(words[1].c_str());
                if (n)
                {
                            dumptree(n, recursive, extended_info, 0, rNpath);
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
    else if (words[0] == "find")
    {
        if (words.size()<2)
        {
            words.push_back(string(".")); //TODO: pensar si meter el pattron via --pattern= y no hacer esta pirula, sino coger nodo de cwd si no hay parametro
        }

        string rNpath = "";

        if (words.size()>1)
        {
            if (words[1].find('/') != string::npos)
            {
                string cwpath = getCurrentPath();
                if (words[1].find_first_of(cwpath) == 0)
                {
                    rNpath = "";
                }
                else
                {
                    rNpath = cwpath;
                }
            }
            n = nodebypath(words[1].c_str());
        }
        else
        {
            n = api->getNodeByHandle(cwd);
        }

        string pattern = getOption(cloptions,"pattern","*");

        struct patternNodeVector pnv;
        pnv.pattern=pattern;
        vector<MegaNode *> listOfMatches;
        pnv.nodesMatching = &listOfMatches;

        processTree(n, includeIfMatchesPattern, (void*)&pnv);
        for (std::vector< MegaNode * >::iterator it = listOfMatches.begin(); it != listOfMatches.end(); ++it)
        {
            MegaNode * n = *it;
            if (n)
            {
                string pathToShow = getDisplayPath(rNpath, n);
                dumpNode(n, 3, 1, pathToShow.c_str());
                //notice: some nodes may be dumped twice

                delete n;
            }
        }

        listOfMatches.clear();
    }
    else if (words[0] == "cd")
    {
        if (words.size() > 1)
        {
            if (( n = nodebypath(words[1].c_str())))
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
            if (!rootNode)
            {
                LOG_err << "nodes not fetched";
                setCurrentOutCode(3);
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
            for (uint i = 1; i < words.size(); i++)
            {
                if (isRegExp(words[i]))
                {
                    vector<MegaNode *> *nodesToDelete = nodesbypath(words[i].c_str());
                    for (std::vector< MegaNode * >::iterator it = nodesToDelete->begin(); it != nodesToDelete->end(); ++it)
                    {
                        MegaNode * nodeToDelete = *it;
                        if (nodeToDelete)
                        {
                            deleteNode(nodeToDelete, api, getFlag(clflags, "r"));
                            delete nodeToDelete;
                        }
                    }

                    nodesToDelete->clear();
                    delete nodesToDelete;
                }
                else
                {
                    words[i]=unquote(words[i]);
                    MegaNode * nodeToDelete = nodebypath(words[i].c_str());
                    if (nodeToDelete)
                    {
                        deleteNode(nodeToDelete, api, getFlag(clflags, "r"));
                        delete nodeToDelete;
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
            if (( n = nodebypath(words[1].c_str())))
            {
                // we have four situations:
                // 1. target path does not exist - fail
                // 2. target node exists and is folder - move
                // 3. target node exists and is file - delete and rename (unless same)
                // 4. target path exists, but filename does not - rename
                if (( tn = nodebypath(words[2].c_str(), NULL, &newname)))
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
                                setCurrentOutCode(3);
                                OUTSTREAM << words[2] << ": Not a directory" << endl;
                                delete tn;
                                delete n;
                                return;
                            }
                            else //move and rename!
                            {
                                MegaCmdListener *megaCmdListener = new MegaCmdListener(NULL);
                                api->moveNode(n, tn, megaCmdListener);
                                megaCmdListener->wait();
                                if (checkNoErrors(megaCmdListener->getError(), "move"))
                                {
                                    MegaCmdListener *megaCmdListener = new MegaCmdListener(NULL);
                                    api->renameNode(n, newname.c_str(), megaCmdListener);
                                    megaCmdListener->wait();
                                    delete megaCmdListener;
                                }
                                else
                                {
                                    LOG_err << "Won't rename, since move failed " << n->getName() << " to " << tn->getName() << " : " << megaCmdListener->getError()->getErrorCode();
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
                                if (tnParentNode)
                                {
                                    delete tnParentNode;

                                    //move into the parent of target node
                                    MegaCmdListener *megaCmdListener = new MegaCmdListener(NULL);
                                    api->moveNode(n, api->getNodeByHandle(tn->getParentHandle()), megaCmdListener);
                                    megaCmdListener->wait();
                                    if (checkNoErrors(megaCmdListener->getError(), "move node"))
                                    {
                                        const char* name_to_replace = tn->getName();

                                        //remove (replaced) target node
                                        if (n != tn) //just in case moving to same location
                                        {
                                            MegaCmdListener *megaCmdListener = new MegaCmdListener(NULL);
                                            api->remove(tn, megaCmdListener); //remove target node
                                            megaCmdListener->wait();
                                            if (!checkNoErrors(megaCmdListener->getError(), "remove target node"))
                                            {
                                                LOG_err << "Couldnt move " << n->getName() << " to " << tn->getName() << " : " << megaCmdListener->getError()->getErrorCode();
                                            }
                                            delete megaCmdListener;
                                        }

                                        // rename moved node with the new name
                                        if (!strcmp(name_to_replace, n->getName()))
                                        {
                                            MegaCmdListener *megaCmdListener = new MegaCmdListener(NULL);
                                            api->renameNode(n, name_to_replace, megaCmdListener);
                                            megaCmdListener->wait();
                                            if (!checkNoErrors(megaCmdListener->getError(), "rename moved node"))
                                            {
                                                LOG_err << "Failed to rename moved node: " << megaCmdListener->getError()->getErrorString();
                                            }
                                            delete megaCmdListener;
                                        }
                                    }
                                    delete megaCmdListener;
                                }
                                else
                                {
                                    setCurrentOutCode(4);
                                    LOG_fatal << "Destiny node is orphan!!!";
                                }
                            }
                            else // target is a folder
                            {
                                MegaCmdListener *megaCmdListener = new MegaCmdListener(NULL);
                                api->moveNode(n, tn, megaCmdListener);
                                megaCmdListener->wait();
                                        checkNoErrors(megaCmdListener->getError(), "move node");
                                delete megaCmdListener;
                            }
                        }
                    }
                    delete tn;
                }
                else //target not found (not even its folder), cant move
                {
                    setCurrentOutCode(3);
                    OUTSTREAM << words[2] << ": No such directory" << endl;
                }
                delete n;
            }
            else
            {
                setCurrentOutCode(3);
                OUTSTREAM << words[1] << ": No such file or directory" << endl;
            }
        }
        else
        {
            setCurrentOutCode(2);
            OUTSTREAM << "      " << getUsageStr("mv") << endl;
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
            if (( n = nodebypath(words[1].c_str())))
            {
                if (( tn = nodebypath(words[2].c_str(), &targetuser, &newname)))
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
                                api->copyNode(n, tn, newname.c_str(), megaCmdListener); //only works for files
                                megaCmdListener->wait();
                                checkNoErrors(megaCmdListener->getError(), "copy node");
                                delete megaCmdListener;
                            }
                            else //copy & rename
                            {
                                //copy with new name
                                MegaCmdListener *megaCmdListener = new MegaCmdListener(NULL);
                                api->copyNode(n, tn, megaCmdListener);
                                megaCmdListener->wait();
                                if (checkNoErrors(megaCmdListener->getError(), "copy node"))
                                {
                                    MegaNode * newNode = api->getNodeByHandle(megaCmdListener->getRequest()->getNodeHandle());
                                    if (newNode)
                                    {
                                        MegaCmdListener *megaCmdListener = new MegaCmdListener(NULL);
                                        api->renameNode(newNode, newname.c_str(), megaCmdListener);
                                        megaCmdListener->wait();
                                        checkNoErrors(megaCmdListener->getError(), "rename new node");
                                        delete megaCmdListener;
                                        delete newNode;
                                    }
                                    else
                                    {
                                        LOG_err << " Couldn't find new node created upon cp";
                                    }
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
                                    if (tnParentNode) // (there should never be any orphaned filenodes)
                                    {
                                        const char* name_to_replace = tn->getName();
                                        //copy with new name
                                        MegaCmdListener *megaCmdListener = new MegaCmdListener(NULL);
                                        api->copyNode(n, tnParentNode, name_to_replace, megaCmdListener);
                                        megaCmdListener->wait();
                                        delete megaCmdListener;
                                        delete tnParentNode;

                                        //remove target node
                                        megaCmdListener = new MegaCmdListener(NULL);
                                        api->remove(tn, megaCmdListener);
                                        megaCmdListener->wait();
                                        checkNoErrors(megaCmdListener->getError(), "delete target node");
                                        delete megaCmdListener;
                                    }
                                    else
                                    {
                                        setCurrentOutCode(4);
                                        LOG_fatal << "Destiny node is orphan!!!";
                                    }
                                }
                                else
                                {
                                    setCurrentOutCode(3);
                                    OUTSTREAM << "Cannot overwrite file with folder" << endl;
                                    return;
                                }
                            }
                            else //copying into folder
                            {
                                MegaCmdListener *megaCmdListener = new MegaCmdListener(NULL);
                                api->copyNode(n, tn, megaCmdListener);
                                megaCmdListener->wait();
                                checkNoErrors(megaCmdListener->getError(), "copy node");
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
                setCurrentOutCode(3);
                OUTSTREAM << words[1] << ": No such file or directory" << endl;
            }
        }
        else
        {
            setCurrentOutCode(3);
            OUTSTREAM << "      " << getUsageStr("cp") << endl;
        }

        return;
    }
    else if (words[0] == "du")
    {
        long long totalSize=0;
        long long currentSize=0;
        string dpath;
        if (words.size()==1)
            words.push_back(".");

        for (int i=1;i < words.size(); i++)
        {
            if (isRegExp(words[i]))
            {
                vector<MegaNode *> *nodesToList = nodesbypath(words[i].c_str());
                if (nodesToList)
                {
                    for (std::vector< MegaNode * >::iterator it = nodesToList->begin(); it != nodesToList->end(); ++it)
                    {
                        MegaNode * n = *it;
                        if (n)
                        {
                            currentSize = api->getSize(n);
                            totalSize += currentSize;
                            dpath = getDisplayPath(words[i],n);
                            OUTSTREAM << dpath << ": " << setw(max(10,(int)(40-dpath.size()))) <<  sizeToText(currentSize) << endl;
                            delete n;
                        }
                    }

                    nodesToList->clear();
                    delete nodesToList;
                }
            }
            else
            {
                words[i]=unquote(words[i]);
                if (!( n = nodebypath(words[i].c_str())))
                {
                    setCurrentOutCode(3);
                    OUTSTREAM << words[i] << ": No such file or directory" << endl;
                    return;
                }

                currentSize = api->getSize(n);
                totalSize += currentSize;
                dpath = getDisplayPath(words[i],n);
                if (dpath.size())
                    OUTSTREAM << dpath << ": " << setw(max(10,(int)(40-dpath.size()))) << sizeToText(currentSize) << endl;
                delete n;
            }
        }
        if (dpath.size())
            OUTSTREAM << "---------------------------------------------" << endl;

        OUTSTREAM << "Total storage used: " << setw(22) << sizeToText(totalSize) << endl;
        //            OUTSTREAM << "Total # of files: " << du.numfiles << endl;
        //            OUTSTREAM << "Total # of folders: " << du.numfolders << endl;

        return;
    }
    else if (words[0] == "get")
    {
        if (words.size() > 1)
        {
            string localPath = getCurrentLocalPath() + "/";

            if (isPublicLink(words[1]))
            {
                if (getLinkType(words[1]) == MegaNode::TYPE_FILE)
                {
                    if (words.size() > 2)
                    {
                        localPath = words[2];
                        if (isFolder(localPath))
                        {
                            localPath += "/";

                            if (!canWrite(localPath))
                            {
                                setCurrentOutCode(3);
                                OUTSTREAM << "Write not allowed in " << localPath << endl;
                                return;
                            }
                        }
                        else
                        {
                            string containingFolder = localPath.substr(0, localPath.find_last_of("/"));
                            if (!isFolder(containingFolder))
                            {
                                setCurrentOutCode(3);
                                OUTSTREAM << containingFolder << " is not a valid Download Folder" << endl;
                                return;
                            }
                            if (!canWrite(containingFolder))
                            {
                                setCurrentOutCode(3);
                                OUTSTREAM << "Write not allowed in " << containingFolder << endl;
                                return;
                            }
                        }
                    }
                    MegaCmdListener *megaCmdListener = new MegaCmdListener(NULL);

                    api->getPublicNode(words[1].c_str(), megaCmdListener);
                    megaCmdListener->wait();

                    if (!megaCmdListener->getError())
                    {
                        LOG_fatal << "No error in listener at get public node";
                    }
                    else if (!checkNoErrors(megaCmdListener->getError(), "get public node"))
                    {
                        if (megaCmdListener->getError()->getErrorCode() == MegaError::API_EARGS)
                        {
                            OUTSTREAM << "ERROR: The link provided might be incorrect: " << words[1].c_str() << endl;
                        }
                        else if (megaCmdListener->getError()->getErrorCode() == MegaError::API_EINCOMPLETE)
                        {
                            OUTSTREAM << "ERROR: The key is missing or wrong " << words[1].c_str() << endl;
                        }
                    }
                    else
                    {
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
                        else
                        {
                            LOG_err << "Empty Request at get";
                        }
                    }
                    delete megaCmdListener;
                }
                else if (getLinkType(words[1]) == MegaNode::TYPE_FOLDER)
                {
                    if (words.size() > 2)
                    {
                        if (isFolder(words[2]))
                        {
                            localPath = words[2] + "/";
                            if (!canWrite(words[2]))
                            {
                                setCurrentOutCode(3);
                                OUTSTREAM << "Write not allowed in " << words[2] << endl;
                                return;
                            }
                        }
                        else
                        {
                            setCurrentOutCode(3);
                            OUTSTREAM << words[2] << " is not a valid Download Folder" << endl;
                            return;
                        }
                    }

                    MegaApi* apiFolder = getFreeApiFolder();
                    char *accountAuth = api->getAccountAuth();
                    apiFolder->setAccountAuth(accountAuth);
                    delete []accountAuth;

                    MegaCmdListener *megaCmdListener = new MegaCmdListener(apiFolder, NULL);
                    apiFolder->loginToFolder(words[1].c_str(), megaCmdListener);
                    megaCmdListener->wait();
                    if (checkNoErrors(megaCmdListener->getError(), "login to folder"))
                    {
                        MegaCmdListener *megaCmdListener2 = new MegaCmdListener(apiFolder, NULL);
                        apiFolder->fetchNodes(megaCmdListener2);
                        megaCmdListener2->wait();
                        if (checkNoErrors(megaCmdListener2->getError(), "access folder link " + words[1]))
                        {
                            MegaNode *folderRootNode = apiFolder->getRootNode();
                            if (folderRootNode)
                            {
                                MegaNode *authorizedNode = apiFolder->authorizeNode(folderRootNode);
                                if (authorizedNode != NULL)
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
                        delete megaCmdListener2;
                    }
                    delete megaCmdListener;
                    freeApiFolder(apiFolder);
                }
                else
                {
                    setCurrentOutCode(2);
                    OUTSTREAM << "Invalid link: " << words[1] << endl;
                }
            }
            else //remote file
            {
                if (isRegExp(words[1]))
                {
                    if (words.size() > 2)
                    {
                        if (isFolder(words[2]))
                        {
                            localPath = words[2] + "/";
                            if (!canWrite(words[2]))
                            {
                                setCurrentOutCode(3);
                                OUTSTREAM << "Write not allowed in " << words[2] << endl;
                                return;
                            }
                        }
                        else
                        {
                            setCurrentOutCode(3);
                            OUTSTREAM << words[2] << " is not a valid Download Folder" << endl;
                            return;
                        }
                    }

                    vector<MegaNode *> *nodesToList = nodesbypath(words[1].c_str());
                    if (nodesToList)
                    {
                        for (std::vector< MegaNode * >::iterator it = nodesToList->begin(); it != nodesToList->end(); ++it)
                        {
                            MegaNode * n = *it;
                            if (n)
                            {
                                downloadNode(localPath, api, n);
                                delete n;
                            }
                        }

                        nodesToList->clear();
                        delete nodesToList;
                    }
                }
                else
                {
                    words[1]=unquote(words[1]);
                    MegaNode *n = nodebypath(words[1].c_str());
                    if (n)
                    {
                        if (words.size() > 2)
                        {
                            if (n->getType() == MegaNode::TYPE_FILE)
                            {
                                localPath = words[2];
                                if (isFolder(localPath))
                                {
                                    localPath += "/";
                                    if (!canWrite(words[2]))
                                    {
                                        setCurrentOutCode(3);
                                        OUTSTREAM << "Write not allowed in " << words[2] << endl;
                                        return;
                                    }
                                }
                                else
                                {
                                    string containingFolder = localPath.substr(0, localPath.find_last_of("/"));
                                    if (!isFolder(containingFolder))
                                    {
                                        setCurrentOutCode(3);
                                        OUTSTREAM << containingFolder << " is not a valid Download Folder" << endl;
                                        return;
                                    }
                                    if (!canWrite(containingFolder))
                                    {
                                        setCurrentOutCode(3);
                                        OUTSTREAM << "Write not allowed in " << containingFolder << endl;
                                        return;
                                    }
                                }
                            }
                            else
                            {
                                if (isFolder(words[2]))
                                {
                                    localPath = words[2] + "/";
                                    if (!canWrite(words[2]))
                                    {
                                        setCurrentOutCode(3);
                                        OUTSTREAM << "Write not allowed in " << words[2] << endl;
                                        return;
                                    }
                                }
                                else
                                {
                                    setCurrentOutCode(3);
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
            string newname = "";
            string localname;
            string destination = "";

            MegaNode *n = NULL;

            if (words.size() > 2)
            {
                destination = words[words.size() - 1];
                n = nodebypath(destination.c_str(), &targetuser, &newname);
            }
            else
            {
                n = api->getNodeByHandle(cwd);
                words.push_back(".");
            }
            if (n)
            {
                if (n->getType() != MegaNode::TYPE_FILE)
                {
                    for (int i = 1; i < max(1, (int)words.size() - 1); i++)
                    {
                        fsAccessCMD->path2local(&words[i], &localname);
                        if (pathExits(localname))
                        {
                            uploadNode(localname, api, n, newname);
                        }
                        else
                        {
                            setCurrentOutCode(3);
                            OUTSTREAM << "Could not find local path " << localname << endl;
                        }
                    }
                }
                else
                {
                    setCurrentOutCode(3);
                    OUTSTREAM << "Destination is not valid (expected folder or alike)" << endl;
                }
                delete n;
            }
            else
            {
                setCurrentOutCode(3);
                OUTSTREAM << "Couln't find destination folder: " << destination << endl;
            }
        }
        else
        {
            setCurrentOutCode(2);
            OUTSTREAM << "      " << getUsageStr("put") << endl;
        }

        return;
    }
    else if (words[0] == "log")
    {
        if (words.size() == 1)
        {
            if (!getFlag(clflags, "s") && !getFlag(clflags, "c"))
            {
                OUTSTREAM << "CMD log level = " << getLogLevelStr(loggerCMD->getCmdLoggerLevel()) << endl;
                OUTSTREAM << "SDK log level = " << getLogLevelStr(loggerCMD->getApiLoggerLevel()) << endl;
            }
            else if (getFlag(clflags, "s"))
            {
                OUTSTREAM << "SDK log level = " << getLogLevelStr(loggerCMD->getApiLoggerLevel()) << endl;
            }
            else if (getFlag(clflags, "c"))
            {
                OUTSTREAM << "CMD log level = " << getLogLevelStr(loggerCMD->getCmdLoggerLevel()) << endl;
            }
        }
        else
        {
            int newLogLevel = getLogLevelNum(words[1].c_str());
            newLogLevel = max(newLogLevel, (int)MegaApi::LOG_LEVEL_FATAL);
            newLogLevel = min(newLogLevel, (int)MegaApi::LOG_LEVEL_MAX);
            if (!getFlag(clflags, "s") && !getFlag(clflags, "c"))
            {
                loggerCMD->setCmdLoggerLevel(newLogLevel);
                loggerCMD->setApiLoggerLevel(newLogLevel);
                OUTSTREAM << "CMD log level = " << getLogLevelStr(loggerCMD->getCmdLoggerLevel()) << endl;
                OUTSTREAM << "SDK log level = " << getLogLevelStr(loggerCMD->getApiLoggerLevel()) << endl;
            }
            else if (getFlag(clflags, "s"))
            {
                loggerCMD->setApiLoggerLevel(newLogLevel);
                OUTSTREAM << "SDK log level = " << getLogLevelStr(loggerCMD->getApiLoggerLevel()) << endl;
            }
            else if (getFlag(clflags, "c"))
            {
                loggerCMD->setCmdLoggerLevel(newLogLevel);
                OUTSTREAM << "CMD log level = " <<getLogLevelStr( loggerCMD->getCmdLoggerLevel()) << endl;
            }
        }

        return;
    }
    else if (words[0] == "pwd")
    {
        string cwpath = getCurrentPath();

        OUTSTREAM << cwpath << endl;

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
                LOG_debug << "Local folder changed to: " << localpath;
            }
            else
            {
                setCurrentOutCode(3);
                LOG_err << "Not a valid folder" << words[1];
            }
        }
        else
        {
            setCurrentOutCode(2);
            OUTSTREAM << "      " << getUsageStr("lcd") << endl;
        }

        return;
    }
    else if (words[0] == "lpwd")
    {
        string cCurrentPath = getCurrentLocalPath();

        OUTSTREAM << cCurrentPath << endl;
        return;
    }
    else if (words[0] == "ipc")
    {
        if (words.size()>1)
        {
            int action;
            string saction;

            if (getFlag(clflags,"a"))
            {
                action = MegaContactRequest::REPLY_ACTION_ACCEPT;
                saction = "Accept";
            }
            else if (getFlag(clflags,"d"))
            {
                action = MegaContactRequest::REPLY_ACTION_DENY;
                saction = "Reject";
            }
            else if (getFlag(clflags,"i"))
            {
                action = MegaContactRequest::REPLY_ACTION_IGNORE;
                saction = "Ignore";
            }
            else
            {
                setCurrentOutCode(2);
                OUTSTREAM << "      " << getUsageStr("ipc") << endl;
            }


            MegaContactRequest * cr;
            string shandle = words[1];
            handle thehandle = api->base64ToUserHandle(shandle.c_str());

            if (shandle.find('@') != string::npos)
            {
                cr=getPcrByContact(shandle);
            }
            else
            {
                cr=api->getContactRequestByHandle(thehandle);
            }
            if (cr)
            {
                MegaCmdListener *megaCmdListener = new MegaCmdListener(api, NULL);
                api->replyContactRequest(cr,action,megaCmdListener);
                megaCmdListener->wait();
                if (checkNoErrors(megaCmdListener->getError(),"reply ipc"))
                {
                    OUTSTREAM << saction << "ed invitation by " << cr->getSourceEmail() << endl;
                }
                delete megaCmdListener;
                delete cr;
            }
            else
            {
                setCurrentOutCode(2);
                OUTSTREAM << "Could not find invitation " << shandle << endl;
            }
        }
        return;
    }
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

                    api->syncFolder(localpath.c_str(), n, megaCmdListener);
                    megaCmdListener->wait();
                    //TODO:  api->addSyncListener();
                    if (checkNoErrors(megaCmdListener->getError(), "sync folder"))
                    {
                        sync_struct *thesync = new sync_struct;
                        thesync->active = true;
                        thesync->handle = megaCmdListener->getRequest()->getNodeHandle();
                        thesync->localpath = string(megaCmdListener->getRequest()->getFile());
                        thesync->fingerprint = megaCmdListener->getRequest()->getNumber();
                        ConfigurationManager::loadedSyncs[megaCmdListener->getRequest()->getFile()] = thesync;

                        OUTSTREAM << "Added sync: " << megaCmdListener->getRequest()->getFile() << " to " << api->getNodePath(n);
                    }

                    delete megaCmdListener;
                }
                else
                {
                    setCurrentOutCode(3);
                    LOG_err << words[2] << ": Syncing requires full access to path, current acces: " << api->getAccess(n);
                }
                delete n;
            }
            else
            {
                setCurrentOutCode(3);
                LOG_err << "Couldn't find remote folder: " << words[2];
            }
        }
        else if (words.size() == 2)
        {
            int id = atoi(words[1].c_str()); //TODO: check if not a number and look by path
            map<string, sync_struct *>::iterator itr;
            int i = 0;
            for (itr = ConfigurationManager::loadedSyncs.begin(); itr != ConfigurationManager::loadedSyncs.end(); i++)
            {
                string key = ( *itr ).first;
                sync_struct *thesync = ((sync_struct*)( *itr ).second );
                MegaNode * n = api->getNodeByHandle(thesync->handle);
                bool erased = false;

                if (n)
                {
                    if (id == i)
                    {
                        int nfiles = 0;
                        int nfolders = 0;
                        nfolders++; //add the share itself
                        int *nFolderFiles = getNumFolderFiles(n, api);
                        nfolders += nFolderFiles[0];
                        nfiles += nFolderFiles[1];
                        delete []nFolderFiles;

                        if (getFlag(clflags, "s"))
                        {
                            OUTSTREAM << "Stopping (disabling)/Resuming sync " << key << " to " << api->getNodePath(n) << endl;
                            MegaCmdListener *megaCmdListener = new MegaCmdListener(NULL);
                            if (thesync->active)
                            {
                                api->disableSync(n, megaCmdListener);
                            }
                            else
                            {
                                api->syncFolder(thesync->localpath.c_str(), n, megaCmdListener);
                            }

                            megaCmdListener->wait();

                            if (checkNoErrors(megaCmdListener->getError(), "stop/resume sync"))
                            {
                                thesync->active = !thesync->active;
                                if (thesync->active) //syncFolder
                                {
                                    if (megaCmdListener->getRequest()->getNumber())
                                    {
                                        thesync->fingerprint = megaCmdListener->getRequest()->getNumber();
                                    }
                                }
                            }
                            delete megaCmdListener;
                        }
                        else if (getFlag(clflags, "d"))
                        {
                            LOG_debug << "Removing sync " << key << " to " << api->getNodePath(n);
                            MegaCmdListener *megaCmdListener = new MegaCmdListener(NULL);
                            if (thesync->active)  //if not active, removeSync will fail.)
                            {
                                api->removeSync(n, megaCmdListener);
                                megaCmdListener->wait();
                                if (checkNoErrors(megaCmdListener->getError(), "remove sync"))
                                {
                                    ConfigurationManager::loadedSyncs.erase(itr++);
                                    erased = true;
                                    delete ( thesync );
                                    OUTSTREAM << "Removed sync " << key << " to " << api->getNodePath(n) << endl;
                                }
                            }
                            else //if !active simply remove
                            {
                                //TODO: if the sdk ever provides a way to clean cache, call it
                                ConfigurationManager::loadedSyncs.erase(itr++);
                                erased = true;
                                delete ( thesync );
                            }
                            delete megaCmdListener;
                        }
                        else
                        {
                            OUTSTREAM << i << ": " << key << " to " << api->getNodePath(n);
                            string sstate(key);
                            sstate = rtrim(sstate, '/');
                            int state = api->syncPathState(&sstate);

                            OUTSTREAM << " - " << ( thesync->active ? "Active" : "Disabled" ) << " - " << getSyncStateStr(state); // << "Active"; //TODO: show inactives
                            OUTSTREAM << ", " << sizeToText(api->getSize(n),false) << "yte(s) in ";
                            OUTSTREAM << nfiles << " file(s) and " << nfolders << " folder(s)" << endl;
                        }
                    }
                    delete n;
                }
                else
                {
                    setCurrentOutCode(3);
                    LOG_err << "Node not found for sync " << key << " into handle: " << thesync->handle;
                }
                if (!erased)
                {
                    ++itr;
                }
            }
        }
        else if (words.size() == 1)
        {
            map<string, sync_struct *>::const_iterator itr;
            int i = 0;
            for (itr = ConfigurationManager::loadedSyncs.begin(); itr != ConfigurationManager::loadedSyncs.end(); ++itr)
            {
                sync_struct *thesync = ((sync_struct*)( *itr ).second );
                MegaNode * n = api->getNodeByHandle(thesync->handle);

                if (n)
                {
                    int nfiles = 0;
                    int nfolders = 0;
                    nfolders++; //add the share itself
                    int *nFolderFiles = getNumFolderFiles(n, api);
                    nfolders += nFolderFiles[0];
                    nfiles += nFolderFiles[1];
                    delete []nFolderFiles;

                    OUTSTREAM << i++ << ": " << ( *itr ).first << " to " << api->getNodePath(n);
                    string sstate(( *itr ).first);
                    sstate = rtrim(sstate, '/');
                    int state = api->syncPathState(&sstate);

                    OUTSTREAM << " - " << (( thesync->active ) ? "Active" : "Disabled" ) << " - " << getSyncStateStr(state); // << "Active"; //TODO: show inactives
                    OUTSTREAM << ", " << sizeToText(api->getSize(n),false) << "yte(s) in ";
                    OUTSTREAM << nfiles << " file(s) and " << nfolders << " folder(s)" << endl;
                    delete n;
                }
                else
                {
                    setCurrentOutCode(3);
                    LOG_err << "Node not found for sync " << ( *itr ).first << " into handle: " << thesync->handle;
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
                        MegaCmdListener *megaCmdListener = new MegaCmdListener(NULL);
                        api->login(words[1].c_str(), words[2].c_str(), megaCmdListener);
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
                    if (( ptr = strchr(words[1].c_str(), '#')))  // folder link indicator
                    {
                        MegaCmdListener *megaCmdListener = new MegaCmdListener(NULL);
                        api->loginToFolder(words[1].c_str(), megaCmdListener);
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
                            api->fastLogin(words[1].c_str(), megaCmdListener);
                            actUponLogin(megaCmdListener);
                            delete megaCmdListener;
                            return;
                        }
                    }
                    setCurrentOutCode(3);
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
     else if (words[0] == "mount")
    {
        listtrees();
        return;
    }
    else if (words[0] == "share")
    {

        string with = getOption(cloptions, "with", "");
        if (( getFlag(clflags, "a") || getFlag(clflags, "d")) && ( "" == with ))
        {
            setCurrentOutCode(2);
            OUTSTREAM << " Required --with destiny" << endl << getUsageStr("share") << endl;
            return;
        }
        int level_NOT_present_value = -214;
        int level = getintOption(cloptions, "level", level_NOT_present_value);
        if (level != level_NOT_present_value && (level < -1 || level > 3) )
        {
            setCurrentOutCode(2);
            OUTSTREAM << "Invalid level of access" << endl;
            return;
        }
        bool listPending = getFlag(clflags, "p");

        if (words.size() <= 1)
        {
            words.push_back(string(""));                  //give at least an empty so that cwd is used
        }
        for (int i = 1; i < (int) words.size(); i++)
        {
            if (isRegExp(words[i]))
            {
                vector<MegaNode *> *nodes = nodesbypath(words[i].c_str());
                if (nodes)
                {
                    if (!nodes->size())
                    {
                        setCurrentOutCode(2);
                        OUTSTREAM << "Nodes not found: " << words[i] << endl;
                    }
                    for (std::vector< MegaNode * >::iterator it = nodes->begin(); it != nodes->end(); ++it)
                    {
                        MegaNode * n = *it;
                        if (n)
                        {
                            if (getFlag(clflags, "a"))
                            {
                                LOG_debug << " sharing ... " << n->getName() << " with " << with;
                                if (level == level_NOT_present_value) level = MegaShare::ACCESS_READ;
                                shareNode(n, with, level);
                            }
                            else if (getFlag(clflags, "d"))
                            {
                                LOG_debug << " deleting share ... " << n->getName();
                                disableShare(n, with);
                                //TODO: disable with all
                            }
                            else
                            {
                                if (level != level_NOT_present_value || with != "")
                                {
                                    setCurrentOutCode(2);
                                    OUTSTREAM << "Unexpected option received. To create/modify a share use -a" << endl;
                                }
                                else if (listPending)
                                {
                                    dumpListOfPendingShares(n, words[i]);
                                }
                                else
                                {
                                    dumpListOfShared(n, words[i]);
                                }
                            }
                            delete n;
                        }
                    }

                    nodes->clear();
                    delete nodes;
                }
                else
                {
                    setCurrentOutCode(2);
                    OUTSTREAM << "Node not found: " << words[i] << endl;
                }
            }
            else // non-wildcard
            {
                words[i]=unquote(words[i]);
                MegaNode *n = nodebypath(words[i].c_str());
                if (n)
                {
                    if (getFlag(clflags, "a"))
                    {
                        LOG_debug << " sharing ... " << n->getName() << " with " << with;
                        if (level == level_NOT_present_value) level = MegaShare::ACCESS_READ;
                        shareNode(n, with, level);
                    }
                    else if (getFlag(clflags, "d"))
                    {
                        LOG_debug << " deleting share ... " << n->getName();
                        disableShare(n, with);
                    }
                    else
                    {
                        if (level != level_NOT_present_value || with != "")
                        {
                            setCurrentOutCode(2);
                            OUTSTREAM << "Unexpected option received. To create/modify a share use -a" << endl;
                        }
                        else if (listPending)
                        {
                            dumpListOfPendingShares(n, words[i]);
                        }
                        else
                        {
                            dumpListOfShared(n, words[i]);
                        }
                    }
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
    else if (words[0] == "users")
    {
        MegaUserList* usersList = api->getContacts();
        if (usersList)
        {
            for (int i = 0; i < usersList->size(); i++)
            {
                MegaUser *user = usersList->get(i);

                if (getFlag(clflags, "d") && words.size()>1 && words[1] == user->getEmail())
                {
                    MegaCmdListener *megaCmdListener = new MegaCmdListener(NULL);
                    api->removeContact(user,megaCmdListener);
                    megaCmdListener->wait();
                    if (checkNoErrors(megaCmdListener->getError(), "delete contact"))
                    {
                        OUTSTREAM << "Contact "<< words[1] << " removed succesfully" << endl;
                    }
                    delete megaCmdListener;
                }
                else
                {
                    if (!(user->getVisibility() != MegaUser::VISIBILITY_VISIBLE && !getFlag(clflags,"h")))
                    {
                        OUTSTREAM << user->getEmail() << ", " << visibilityToString(user->getVisibility());
                        if (user->getTimestamp())
                        {
                            OUTSTREAM << " since " << getReadableTime(user->getTimestamp());
                        }
                        OUTSTREAM << endl;

                        if (getFlag(clflags, "s"))
                        {
                            MegaShareList *shares = api->getOutShares();
                            if (shares)
                            {
                                bool first_share = true;
                                for (int j = 0; j < shares->size(); j++)
                                {
                                    if (!strcmp(shares->get(j)->getUser(), user->getEmail()))
                                    {
                                        MegaNode * n = api->getNodeByHandle(shares->get(j)->getNodeHandle());
                                        if (n)
                                        {
                                            if (first_share)
                                            {
                                                OUTSTREAM << "\tSharing:" << endl;
                                                first_share = false;
                                            }

                                            OUTSTREAM << "\t";
                                            dumpNode(n, 2, 0, getDisplayPath("/", n).c_str());
                                            delete n;
                                        }
                                    }
                                }

                                delete shares;
                            }
                        }
                    }
                }
            }

            delete usersList;
        }

        return;
    }
    else if (words[0] == "mkdir")
    {
        for (int i=1;i<words.size();i++)
        {
            MegaNode *currentnode = api->getNodeByHandle(cwd);
            if (currentnode)
            {
                string rest = words[i];
                while (rest.length())
                {
                    bool lastleave = false;
                    size_t possep = rest.find_first_of("/");
                    if (possep == string::npos)
                    {
                        possep = rest.length();
                        lastleave = true;
                    }

                    string newfoldername = rest.substr(0, possep);
                    if (!rest.length())
                    {
                        break;
                    }
                    if (newfoldername.length())
                    {
                        MegaNode *existing_node = api->getChildNode(currentnode, newfoldername.c_str());
                        if (!existing_node)
                        {
                            if (!getFlag(clflags, "p") && !lastleave)
                            {
                                setCurrentOutCode(2);
                                OUTSTREAM << "Use -p to create folders recursively" << endl;
                                delete currentnode;
                                return;
                            }
                            LOG_verbose << "Creating (sub)folder: " << newfoldername;
                            MegaCmdListener *megaCmdListener = new MegaCmdListener(NULL);
                            api->createFolder(newfoldername.c_str(), currentnode, megaCmdListener);
                            actUponCreateFolder(megaCmdListener);
                            delete megaCmdListener;
                            MegaNode *prevcurrentNode = currentnode;
                            currentnode = api->getChildNode(currentnode, newfoldername.c_str());
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
                            currentnode = existing_node;
                        }

                        if (lastleave && existing_node)
                        {
                            setCurrentOutCode(3);
                            LOG_err << "Folder already exists: " << words[i];
                        }
                    }

                    //string rest = rest.substr(possep+1,rest.length()-possep-1);
                    if (!lastleave)
                    {
                        rest = rest.substr(possep + 1, rest.length());
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
                setCurrentOutCode(2);
                OUTSTREAM << "      " << getUsageStr("mkdir") << endl;
            }
        }
        return;
    }
    else if (words[0] == "attr")
    {
        if (words.size() > 1)
        {
            int cancel = getFlag(clflags,"d");
            bool settingattr = getFlag(clflags,"s");

            string nodePath = words.size()>1?words[1]:"";
            string attribute = words.size()>2?words[2]:"";
            string attrValue = words.size()>3?words[3]:"";
            n = nodebypath(nodePath.c_str());

            if (n)
            {
                if (settingattr || cancel)
                {
                    if (attribute.size())
                    {
                        const char *cattrValue = cancel?NULL:attrValue.c_str();
                        MegaCmdListener *megaCmdListener = new MegaCmdListener(NULL);
                        api->setCustomNodeAttribute(n, attribute.c_str(), cattrValue, megaCmdListener);
                        megaCmdListener->wait();
                        if (checkNoErrors(megaCmdListener->getError(), "set node attribute: "+attribute))
                        {
                            OUTSTREAM << "Node attribute " << attribute << (cancel?" removed":" updated") << " correctly" << endl;
                            delete n;
                            n = api->getNodeByHandle(megaCmdListener->getRequest()->getNodeHandle());
                        }
                        delete megaCmdListener;
                    }
                    else
                    {
                        setCurrentOutCode(2);
                        OUTSTREAM << "Attribute not specified" << endl;
                        OUTSTREAM << "      " << getUsageStr("attr") << endl;
                        return;
                    }
                }

                //List node custom attributes
                MegaStringList *attrlist = n->getCustomAttrNames();
                if (attrlist)
                {
                    if (!attribute.size())
                    {
                        OUTSTREAM << "The node has " << attrlist->size() << " attributes" << endl;
                    }
                    for (int a=0;a<attrlist->size();a++)
                    {
                        string iattr = attrlist->get(a);
                        if (!attribute.size() || attribute == iattr)
                        {
                            const char* iattrval = n->getCustomAttr(iattr.c_str());
                            OUTSTREAM << "\t" << iattr << " = " << (iattrval?iattrval:"NULL") << endl;
                        }
                    }
                    delete attrlist;
                }

                delete n;
            }
            else
            {
                setCurrentOutCode(3);
                OUTSTREAM << "Couldn't find node: " << nodePath << endl;
                return;
            }
        }
        else
        {
            setCurrentOutCode(2);
            OUTSTREAM << "      " << getUsageStr("attr") << endl;
            return;
        }


        return;
    }
    else if (words[0] == "userattr")
    {
        //TODO: implement --load=file option
        bool settingattr = getFlag(clflags,"s");

        int attribute = getAttrNum(words.size()>1?words[1].c_str():"-1");
        string attrValue = words.size()>2?words[2]:"";
        string user = getOption(cloptions,"user","");

        if (settingattr)
        {
            if (attribute != -1)
            {
                MegaCmdListener *megaCmdListener = new MegaCmdListener(NULL);
                api->setUserAttribute(attribute,attrValue.c_str(), megaCmdListener);
                megaCmdListener->wait();
                if (checkNoErrors(megaCmdListener->getError(), string("set user attribute ")+getAttrStr(attribute)))
                {
                    OUTSTREAM << "User attribute " << getAttrStr(attribute) << " updated" << " correctly" << endl;
                }
                else
                {
                    delete megaCmdListener;
                    return;
                }
                delete megaCmdListener;
            }
            else
            {
                setCurrentOutCode(2);
                OUTSTREAM << "Attribute not specified" << endl;
                OUTSTREAM << "      " << getUsageStr("userattr") << endl;
                return;
            }
        }

        for (int a=(attribute==-1?0:attribute);a<(attribute==-1?10:attribute+1);a++)
        {
            MegaCmdListener *megaCmdListener = new MegaCmdListener(NULL);
            if (user.size())
            {
//                MegaUser *u = api->getContact(user.c_str());
                api->getUserAttribute(user.c_str(),a, megaCmdListener);
//                delete u;
            }
            else
            {
                api->getUserAttribute(a, megaCmdListener);
            }
            megaCmdListener->wait();
            if (checkNoErrors(megaCmdListener->getError(), string("get user attribute ")+getAttrStr(a)))
            {
                int iattr = megaCmdListener->getRequest()->getParamType();
                const char *value = megaCmdListener->getRequest()->getText();
                string svalue;
                try
                {
                    svalue=string(value);
                }
                catch(exception e)
                {
                    svalue="NOT PRINTABLE";
                }
                OUTSTREAM << "\t" << getAttrStr(iattr) << " = " << svalue << endl;
            }

            delete megaCmdListener;
        }
        return;
    }
    else if (words[0] == "thumbnail")
    {
        if (words.size() > 1)
        {
            string nodepath=words[1];
            string localpath=words.size()>2?words[2]:"./";
            n = nodebypath(nodepath.c_str());
            if (n)
            {
                MegaCmdListener *megaCmdListener = new MegaCmdListener(NULL);
                bool setting = getFlag(clflags,"s");
                if (setting)
                {
                    api->setThumbnail(n,localpath.c_str(),megaCmdListener);
                }
                else
                {
                    api->getThumbnail(n,localpath.c_str(),megaCmdListener);
                }
                megaCmdListener->wait();
                if (checkNoErrors(megaCmdListener->getError(), (setting?"set thumbnail ":"get thumbnail ")+nodepath + " to "+localpath))
                {
                    OUTSTREAM << "Thumbnail for " << nodepath << (setting?" loaded from ":" saved in ") << megaCmdListener->getRequest()->getFile() << endl;
                }
                delete megaCmdListener;
                delete n;
            }
        }
        else
        {
            setCurrentOutCode(2);
            OUTSTREAM << "      " << getUsageStr("attr") << endl;
            return;
        }
        return;
    }
    else if (words[0] == "preview")
    {
        if (words.size() > 1)
        {
            string nodepath=words[1];
            string localpath=words.size()>2?words[2]:"./";
            n = nodebypath(nodepath.c_str());
            if (n)
            {
                MegaCmdListener *megaCmdListener = new MegaCmdListener(NULL);
                bool setting = getFlag(clflags,"s");
                if (setting)
                {
                    api->setPreview(n,localpath.c_str(),megaCmdListener);
                }
                else
                {
                    api->getPreview(n,localpath.c_str(),megaCmdListener);
                }
                megaCmdListener->wait();
                if (checkNoErrors(megaCmdListener->getError(), (setting?"set preview ":"get preview ")+nodepath + " to "+localpath))
                {
                    OUTSTREAM << "Preview for " << nodepath << (setting?" loaded from ":" saved in ") << megaCmdListener->getRequest()->getFile() << endl;
                }
                delete megaCmdListener;
                delete n;
            }
        }
        else
        {
            setCurrentOutCode(2);
            OUTSTREAM << "      " << getUsageStr("attr") << endl;
            return;
        }
        return;
    }
    else if (words[0] == "debug")
    {
        vector<string> newcom;
        newcom.push_back("log");
        newcom.push_back("5");

        return executecommand(newcom,clflags,cloptions);
    }
    else if (words[0] == "passwd")
    {
        if (api->isLoggedIn())
        {
            if (words.size() == 1)
            {
                setprompt(OLDPASSWORD);
            }
            else if (words.size() > 2)
            {
                changePassword(words[1].c_str(), words[2].c_str());
            }
            else
            {
                setCurrentOutCode(2);
                OUTSTREAM << "      " << getUsageStr("passwd") << endl;
            }
        }
        else
        {
            setCurrentOutCode(3);
            OUTSTREAM << "Not logged in." << endl;
        }

        return;
    }
    else if (words[0] == "putbps")
    {
        int uploadLimit;

        if (words.size() > 1)
        {
            if (words[1] == "auto")
            {
                uploadLimit = -1;
            }
            else if (words[1] == "none")
            {
                uploadLimit = -1;
            }
            else
            {
                uploadLimit = atoi(words[1].c_str());
            }

            LOG_debug << "Setting Upload limit to " << uploadLimit << " byte(s)/second";
            api->setUploadLimit(uploadLimit);
        }
        //TODO: after https://github.com/meganz/sdk/pull/316 redo this part and include outputting the value

        return;
    }
    else if (words[0] == "invite")
    {
        if (words.size() > 1)
        {
            string email = words[1];
            if (( email.find("@") == string::npos )
                || ( email.find(".") == string::npos )
                || ( email.find("@") > email.find(".")))
            {
                setCurrentOutCode(6);
                OUTSTREAM << "No valid email provided" << endl;
                OUTSTREAM << "      " << getUsageStr("invite") << endl;
            }
            else
            {
                int action = MegaContactRequest::INVITE_ACTION_ADD;
                if (getFlag(clflags, "d"))
                {
                    action = MegaContactRequest::INVITE_ACTION_DELETE;
                }
                if (getFlag(clflags, "r"))
                {
                    action = MegaContactRequest::INVITE_ACTION_REMIND;
                }

                string message = getOption(cloptions, "message", "");
                MegaCmdListener *megaCmdListener = new MegaCmdListener(NULL);
                api->inviteContact(email.c_str(), message.c_str(), action, megaCmdListener);
                megaCmdListener->wait();
                if (checkNoErrors(megaCmdListener->getError(), "(re)invite user"))
                {
                    OUTSTREAM << "Invitation sent to user: " << email << endl;
                }
                else if (megaCmdListener->getError()->getErrorCode() == MegaError::API_EACCESS)
                {
                    setCurrentOutCode(megaCmdListener->getError()->getErrorCode());
                    OUTSTREAM << "Reminder not yet available: " << " available after 15 days";
                    MegaContactRequestList *ocrl = api->getOutgoingContactRequests();
                    if (ocrl)
                    {
                        for (int i = 0; i < ocrl->size(); i++)
                        {
                            if (ocrl->get(i)->getTargetEmail() && megaCmdListener->getRequest()->getEmail() && !strcmp(ocrl->get(i)->getTargetEmail(), megaCmdListener->getRequest()->getEmail()))
                            {
                                 OUTSTREAM << " (" << getReadableTime(getTimeStampAfter(ocrl->get(i)->getModificationTime(),"15d")) << ")";
                            }
                        }
                       delete ocrl;
                    }

                    OUTSTREAM << endl;
                }
                delete megaCmdListener;
            }
        }

        return;
    }
    else if (words[0] == "signup")
    {
        if (api->isLoggedIn())
        {
            setCurrentOutCode(2);
            OUTSTREAM << "Please loggout first " << endl;
        }
        else if (words.size() > 1)
        {
            string email = words[1];
            string name = getOption(cloptions, "name", email);
            if (words.size() > 2)
            {
                string passwd = words[2];
                MegaCmdListener *megaCmdListener = new MegaCmdListener(NULL);
                api->createAccount(email.c_str(),passwd.c_str(),name.c_str(),megaCmdListener);
                megaCmdListener->wait();
                if (checkNoErrors(megaCmdListener->getError(), "create account <"+email+">"))
                {
                    OUTSTREAM << "Account <" << email << "> created succesfully. You will receive a confirmation link. Use \"confirm\" with the provided link to confirm that account" << endl;
                }
                delete megaCmdListener;
            }
            else
            {
                //TODO: play with prompt
            }
        }
        else
        {
            setCurrentOutCode(2);
            OUTSTREAM << "      " << getUsageStr("signup") << endl;
        }

        return;
    }
    else if (words[0] == "whoami")
    {
        MegaUser *u = api->getMyUser();
        if (u)
        {
            OUTSTREAM << "Account e-mail: " << u->getEmail() << endl;
            if (getFlag(clflags, "l"))
            {
                MegaCmdListener *megaCmdListener = new MegaCmdListener(NULL);
                api->getExtendedAccountDetails(true, true, true, megaCmdListener);
                actUponGetExtendedAccountDetails(megaCmdListener);
                delete megaCmdListener;
            }
            delete u;
        }
        else
        {
            setCurrentOutCode(3);
            OUTSTREAM << "Not logged in." << endl;
        }

        return;
    }
    else if (words[0] == "export")
    {
        time_t expireTime = 0;
        string sexpireTime = getOption(cloptions, "expire", "");
        if ("" != sexpireTime)
        {
            expireTime = getTimeStampAfter(sexpireTime);
        }
        if (expireTime < 0)
        {
            setCurrentOutCode(2);
            OUTSTREAM << "Invalid time " << sexpireTime << endl;
            return;
        }

        if (words.size() <= 1)
        {
            words.push_back(string("")); //give at least an empty so that cwd is used
        }
        for (int i = 1; i < (int) words.size(); i++)
        {
            if (isRegExp(words[i]))
            {
                vector<MegaNode *> *nodes = nodesbypath(words[i].c_str());
                if (nodes)
                {
                    if (!nodes->size())
                    {
                        setCurrentOutCode(2);
                        OUTSTREAM << "Nodes not found: " << words[i] << endl;
                    }
                    for (std::vector< MegaNode * >::iterator it = nodes->begin(); it != nodes->end(); ++it)
                    {
                        MegaNode * n = *it;
                        if (n)
                        {
                            if (getFlag(clflags, "a"))
                            {
                                LOG_debug << " exporting ... " << n->getName() << " expireTime=" << expireTime;
                                exportNode(n, expireTime);
                            }
                            else if (getFlag(clflags, "d"))
                            {
                                LOG_debug << " deleting export ... " << n->getName();
                                disableExport(n);
                            }
                            else
                            {
                                dumpListOfExported(n, words[i]);
                            }
                            delete n;
                        }
                    }

                    nodes->clear();
                    delete nodes;
                }
                else
                {
                    setCurrentOutCode(2);
                    OUTSTREAM << "Node not found: " << words[i] << endl;
                }
            }
            else
            {
                words[i]=unquote(words[i]);
                MegaNode *n = nodebypath(words[i].c_str());
                if (n)
                {
                    if (getFlag(clflags, "a"))
                    {
                        LOG_debug << " exporting ... " << n->getName();
                        exportNode(n, expireTime);
                    }
                    else if (getFlag(clflags, "d"))
                    {
                        LOG_debug << " deleting export ... " << n->getName();
                        disableExport(n);
                    }
                    else
                    {
                        dumpListOfExported(n, words[i]);
                    }
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
        string remotePath = "";
        MegaNode *dstFolder;
        if (words.size() > 1) //link
        {
            if (isPublicLink(words[1]))
            {
                if (words.size() > 2)
                {
                    remotePath = words[2];
                    dstFolder = nodebypath(remotePath.c_str());
                }
                else
                {
                    dstFolder = api->getNodeByHandle(cwd);
                    remotePath = "."; //just to inform (alt: getpathbynode)
                }
                if (dstFolder && ( !dstFolder->getType() == MegaNode::TYPE_FILE ))
                {
                    if (getLinkType(words[1]) == MegaNode::TYPE_FILE)
                    {
                        MegaCmdListener *megaCmdListener = new MegaCmdListener(NULL);

                        api->importFileLink(words[1].c_str(), dstFolder, megaCmdListener);
                        megaCmdListener->wait();
                        if (checkNoErrors(megaCmdListener->getError(), "import node"))
                        {
                            MegaNode *imported = api->getNodeByHandle(megaCmdListener->getRequest()->getNodeHandle());
                            char *importedPath = api->getNodePath(imported);
                            LOG_info << "Import file complete: " << importedPath;
                            delete imported;
                            delete []importedPath;
                        }

                        delete megaCmdListener;
                    }
                    else if (getLinkType(words[1]) == MegaNode::TYPE_FOLDER)
                    {
                        MegaApi* apiFolder = getFreeApiFolder();
                        char *accountAuth = api->getAccountAuth();
                        apiFolder->setAccountAuth(accountAuth);
                        delete []accountAuth;

                        MegaCmdListener *megaCmdListener = new MegaCmdListener(apiFolder, NULL);
                        apiFolder->loginToFolder(words[1].c_str(), megaCmdListener);
                        megaCmdListener->wait();
                        if (checkNoErrors(megaCmdListener->getError(), "login to folder"))
                        {
                            MegaCmdListener *megaCmdListener2 = new MegaCmdListener(apiFolder, NULL);
                            apiFolder->fetchNodes(megaCmdListener2);
                            megaCmdListener2->wait();
                            if (checkNoErrors(megaCmdListener2->getError(), "access folder link " + words[1]))
                            {
                                MegaNode *folderRootNode = apiFolder->getRootNode();
                                if (folderRootNode)
                                {
                                    MegaNode *authorizedNode = apiFolder->authorizeNode(folderRootNode);
                                    if (authorizedNode != NULL)
                                    {
                                        MegaCmdListener *megaCmdListener3 = new MegaCmdListener(apiFolder, NULL);
                                        api->copyNode(authorizedNode, dstFolder, megaCmdListener3);
                                        megaCmdListener3->wait();
                                        if (checkNoErrors(megaCmdListener->getError(), "import folder node"))
                                        {
                                            MegaNode *importedFolderNode = api->getNodeByHandle(megaCmdListener3->getRequest()->getNodeHandle());
                                            char *pathnewFolder = api->getNodePath(importedFolderNode);
                                            if (pathnewFolder)
                                            {
                                                OUTSTREAM << "Imported folder complete: " << pathnewFolder << endl;
                                                delete []pathnewFolder;
                                            }
                                            delete importedFolderNode;
                                        }
                                        delete megaCmdListener3;
                                        delete authorizedNode;
                                    }
                                    else
                                    {
                                        setCurrentOutCode(3);
                                        LOG_debug << "Node couldn't be authorized: " << words[1];
                                    }
                                    delete folderRootNode;
                                }
                                else
                                {
                                    setCurrentOutCode(3);
                                    LOG_err << "Couldn't get root folder for folder link";
                                }
                            }
                            delete megaCmdListener2;
                        }
                        delete megaCmdListener;
                        freeApiFolder(apiFolder);
                        delete dstFolder;
                    }
                    else
                    {
                        setCurrentOutCode(4);
                        OUTSTREAM << "Invalid link: " << words[1] << endl;
                        OUTSTREAM << "      " << getUsageStr("import") << endl;
                    }
                }
                else
                {
                        setCurrentOutCode(4);
                    OUTSTREAM << "Invalid destiny: " << remotePath << endl;
                }
            }
            else
            {
                        setCurrentOutCode(3);
                OUTSTREAM << "Invalid link: " << words[1] << endl;
            }
        }
        else
        {
            setCurrentOutCode(2);
            OUTSTREAM << "      " << getUsageStr("import") << endl;
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
        bool deleteSession = getFlag(clflags,"delete-session");
        if (deleteSession)
        {
            api->logout(megaCmdListener);
        }
        else //local logout
        {
            api->localLogout(megaCmdListener);
        }
        actUponLogout(megaCmdListener,deleteSession);
        if (!deleteSession)
        {
            OUTSTREAM << "Session close but not deleted. Warning: it will be restored the next time you execute the application. Execute \"logout --delete-session\" to delete the session permanently." << endl;
        }
        delete megaCmdListener;

        return;
    }
    else if (words[0] == "confirm")
    {
        if (words.size() > 2)
        {
            string link = words[1];
            string email = words[2];
            // check email corresponds with link:
            MegaCmdListener *megaCmdListener = new MegaCmdListener(NULL);
            api->querySignupLink(link.c_str(),megaCmdListener);
            megaCmdListener->wait();
            if (checkNoErrors(megaCmdListener->getError(),"check email corresponds to link"))
            {
                if (email == megaCmdListener->getRequest()->getEmail())
                {
                    string passwd;
                    if ( words.size() > 3 )
                    {
                        passwd = words[3];
                        MegaCmdListener *megaCmdListener2 = new MegaCmdListener(NULL);
                        api->confirmAccount(link.c_str(),passwd.c_str(),megaCmdListener2);
                        megaCmdListener2->wait();
                        if (checkNoErrors(megaCmdListener2->getError(),"confirm account"))
                        {
                            OUTSTREAM << "Account " << email << " confirmed succesfully. You can login with it now" << endl;
                        }
                        delete megaCmdListener2;
                    }
                    else
                    {
                        //TODO: play with password promt
                    }
                }
                else
                {
                    setCurrentOutCode(6);
                    OUTSTREAM << email << " doesn't correspond to the confirmation link: " << link << endl;
                }
            }

            delete megaCmdListener;
        }
        else
        {
            setCurrentOutCode(2);
            OUTSTREAM << "      " << getUsageStr("confirm") << endl;
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
            setCurrentOutCode(3);
            OUTSTREAM << "Not logged in." << endl;
        }
        return;
    }
    else if (words[0] == "history")
    {
        printHistory();
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
        return;
    }
    else if (words[0] == "showpcr")
    {
        MegaContactRequestList *ocrl = api->getOutgoingContactRequests();
        if (ocrl)
        {
            if (ocrl->size())
                OUTSTREAM << "Outgoing PCRs:" << endl;
            for (int i = 0; i < ocrl->size(); i++)
            {
                MegaContactRequest * cr = ocrl->get(i);
                OUTSTREAM << " " << setw(22) << cr->getTargetEmail();

                char * sid = api->userHandleToBase64(cr->getHandle());

                OUTSTREAM << "\t (id: " << sid << ", creation: " << getReadableTime(cr->getCreationTime())
                          << ", modification: " << getReadableTime(cr->getModificationTime()) << ")";
                //                                OUTSTREAM << ": " << cr->getSourceMessage();

                delete[] sid;
                OUTSTREAM << endl;
            }

            delete ocrl;
        }
        MegaContactRequestList *icrl = api->getIncomingContactRequests();
        if (icrl)
        {
            if (icrl->size())
                OUTSTREAM << "Incoming PCRs:" << endl;

            for (int i = 0; i < icrl->size(); i++)
            {
                MegaContactRequest * cr = icrl->get(i);
                OUTSTREAM << " " << setw(22) << cr->getSourceEmail();

                MegaHandle id = cr->getHandle();
                char sid[12];
                Base64::btoa((byte*)&( id ), sizeof( id ), sid);

                OUTSTREAM << "\t (id: " << sid << ", creation: " << getReadableTime(cr->getCreationTime())
                          << ", modification: " << getReadableTime(cr->getModificationTime()) << ")";
                if (cr->getSourceMessage())
                {
                    OUTSTREAM << endl << "\t" << "Invitation message: " << cr->getSourceMessage();
                }

                OUTSTREAM << endl;
            }

            delete icrl;
        }
        return;
    }
    else if (words[0] == "killsession")
    {
        string thesession;
        MegaHandle thehandle = UNDEF;
        if (getFlag(clflags,"a"))
        {
            // Kill all sessions (except current)
            thesession="all";
            thehandle = mega::INVALID_HANDLE;
        }
        else if (words.size()>1)
        {
            thesession = words[1];
            thehandle = api->base64ToUserHandle(thesession.c_str());
        }
        else
        {
            setCurrentOutCode(2);
            OUTSTREAM << "      " << getUsageStr("killsession") << endl;
            return;
        }

        MegaCmdListener *megaCmdListener = new MegaCmdListener(NULL);
        api->killSession(thehandle,megaCmdListener);
        megaCmdListener->wait();
        if (checkNoErrors(megaCmdListener->getError(), "kill session "+thesession+". Maybe the session was not valid."))
        {
            OUTSTREAM << "Session "<< thesession << " killed successfully" << endl;
        }

        delete megaCmdListener;
        return;
    }
    else if (words[0] == "locallogout")
    {
        OUTSTREAM << "Logging off locally..." << endl;
        cwd = UNDEF;
        return;
    }
    else
    {
        setCurrentOutCode(1);
        OUTSTREAM << "Invalid command:" << words[0] << endl;
    }
}

bool MegaCmdExecuter::checkNoErrors(MegaError *error, string message)
{
    if (!error)
    {
        LOG_fatal << "No MegaError at request: " << message;
        return false;
    }
    if (error->getErrorCode() == MegaError::API_OK)
    {
        return true;
    }

    setCurrentOutCode(error->getErrorCode());
    OUTSTREAM << "Failed to " << message << ": " << error->getErrorString() << endl;
    return false;
}
