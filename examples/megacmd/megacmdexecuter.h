/**
 * @file examples/megacmd/megacmdexecuter.h
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

#ifndef MEGACMDEXECUTER_H
#define MEGACMDEXECUTER_H

#include "synchronousrequestlistener.h"
#include "megacmdlogger.h"

using namespace mega;

class MegaCmdExecuter
{
private:
    MegaApi *api;
    handle cwd;
    char *session;
    MegaFileSystemAccess *fsAccessCMD;
    MegaCMDLogger *loggerCMD;
    MegaMutex mtxSyncMap;

    // login/signup e-mail address
    string login;

    // signup name
    string name;

    // link to confirm
    string link;

    void updateprompt(MegaApi *api, MegaHandle handle);

public:
    bool signingup = false;
    bool confirming = false;

    MegaCmdExecuter(MegaApi *api, MegaCMDLogger *loggerCMD);
    ~MegaCmdExecuter();

    // nodes browsing
    void listtrees();
    static bool includeIfIsExported(MegaApi* api, MegaNode * n, void *arg);
    static bool includeIfIsShared(MegaApi* api, MegaNode * n, void *arg);
    static bool includeIfIsPendingOutShare(MegaApi* api, MegaNode * n, void *arg);
    static bool includeIfIsSharedOrPendingOutShare(MegaApi* api, MegaNode * n, void *arg);
    static bool includeIfMatchesPattern(MegaApi* api, MegaNode * n, void *arg);
    bool processTree(MegaNode * n, bool(MegaApi *, MegaNode *, void *), void *( arg ));
    MegaNode* nodebypath(const char* ptr, string* user = NULL, string* namepart = NULL);
    void getNodesMatching(MegaNode *parentNode, queue<string> pathParts, vector<MegaNode *> *nodesMatching);
    MegaNode * getRootNodeByPath(const char *ptr, string* user = NULL);
    vector <MegaNode*> * nodesbypath(const char* ptr, string* user = NULL, string* namepart = NULL);
    void dumpNode(MegaNode* n, int extended_info, int depth = 0, const char* title = NULL);
    void dumptree(MegaNode* n, int recurse, int extended_info, int depth = 0, string pathRelativeTo = "NULL");
    MegaContactRequest * getPcrByContact(string contactEmail);
    string getDisplayPath(string givenPath, MegaNode* n);
    void dumpListOfExported(MegaNode* n, string givenPath);
    void listnodeshares(MegaNode* n, string name);
    void dumpListOfShared(MegaNode* n, string givenPath);
    void dumpListOfAllShared(MegaNode* n, string givenPath);
    void dumpListOfPendingShares(MegaNode* n, string givenPath);
    string getCurrentPath();

    //acting
    void loginWithPassword(char *password);
    void changePassword(const char *oldpassword, const char *newpassword);
    void actUponGetExtendedAccountDetails(SynchronousRequestListener *srl, int timeout = -1);
    bool actUponFetchNodes(MegaApi * api, SynchronousRequestListener *srl, int timeout = -1);
    void actUponLogin(SynchronousRequestListener *srl, int timeout = -1);
    void actUponLogout(SynchronousRequestListener *srl, bool deletedSession, int timeout = 0);
    int actUponCreateFolder(SynchronousRequestListener *srl, int timeout = 0);
    void deleteNode(MegaNode *nodeToDelete, MegaApi* api, int recursive);
    void downloadNode(string localPath, MegaApi* api, MegaNode *node);
    void uploadNode(string localPath, MegaApi* api, MegaNode *node, string newname);
    void exportNode(MegaNode *n, int expireTime);
    void disableExport(MegaNode *n);
    void shareNode(MegaNode *n, string with, int level = MegaShare::ACCESS_READ);
    void disableShare(MegaNode *n, string with);
    vector<string> listpaths(string askedPath = "", bool discardFiles = false);
    vector<string> getlistusers();
    vector<string> getNodeAttrs(string nodePath);
    vector<string> getsessions();

    void executecommand(vector<string> words, map<string, int> *clflags, map<string, string> *cloptions);

    bool checkNoErrors(MegaError *error, string message = "");

    //doomedtodie
    void syncstat(Sync* sync);
    const char* treestatename(treestate_t ts);
    bool is_syncable(const char* name);
    int loadfile(string* name, string* data);
    void signup(string name, string passwd, string email);
    void signupWithPassword(string passwd);
    void confirm(string passwd, string email, string link);
    void confirmWithPassword(string passwd);

    int makedir(string remotepath, bool recursive, MegaNode *parentnode = NULL);
};

#endif // MEGACMDEXECUTER_H
