#ifndef MEGACMDEXECUTER_H
#define MEGACMDEXECUTER_H

#include "mega.h"
#include "megaapi.h"

#include "synchronousrequestlistener.h"
#include "megacmdlogger.h"

using namespace mega;

class MegaCmdExecuter
{
private:
    MegaApi *api;
    handle cwd = UNDEF;
    char *session;
    MegaFileSystemAccess *fsAccessCMD;
    MegaCMDLogger *loggerCMD;
    MegaMutex mtxSyncMap;
    // login e-mail address
    string login;

    void updateprompt(MegaApi *api, MegaHandle handle);

public:
    MegaCmdExecuter(MegaApi *api, MegaCMDLogger *loggerCMD);
    ~MegaCmdExecuter();

    // nodes browsing
    void listtrees();
    static bool includeIfIsExported(MegaApi* api, MegaNode * n, void *arg);
    static bool includeIfIsShared(MegaApi* api, MegaNode * n, void *arg);
    static bool includeIfIsPendingOutShare(MegaApi* api, MegaNode * n, void *arg);
    static bool includeIfIsSharedOrPendingOutShare(MegaApi* api, MegaNode * n, void *arg);
    bool processTree(MegaNode *n, bool (MegaApi *, MegaNode *, void *),void *(arg));
    MegaNode* nodebypath(const char* ptr, string* user = NULL, string* namepart = NULL);
    void getNodesMatching(MegaNode *parentNode, queue<string> pathParts, vector<MegaNode *> *nodesMatching);
    MegaNode * getRootNodeByPath(const char *ptr, string* user = NULL);
    vector <MegaNode*> * nodesbypath(const char* ptr, string* user = NULL, string* namepart = NULL);
    void dumpNode(MegaNode* n, int extended_info, int depth = 0, const char* title = NULL);
    void dumptree(MegaNode* n, int recurse, int extended_info, int depth = 0, string pathRelativeTo = "NULL");
    void nodepath(handle h, string* path);
    string getDisplayPath(string givenPath, MegaNode* n);
    void dumpListOfExported(MegaNode* n, string givenPath);
    void listnodeshares(MegaNode* n, string name);
    void dumpListOfShared(MegaNode* n, string givenPath);
    void dumpListOfAllShared(MegaNode* n, string givenPath);
    void dumpListOfPendingShares(MegaNode* n, string givenPath);

    //acting
    void loginWithPassword(char *password);
    void actUponGetExtendedAccountDetails(SynchronousRequestListener *srl,int timeout=-1);
    bool actUponFetchNodes(MegaApi * api, SynchronousRequestListener *srl,int timeout=-1);
    void actUponLogin(SynchronousRequestListener *srl,int timeout=-1);
    void actUponLogout(SynchronousRequestListener *srl,int timeout=0);
    int actUponCreateFolder(SynchronousRequestListener *srl,int timeout=0);
    void deleteNode(MegaNode *nodeToDelete, MegaApi* api, int recursive);
    void downloadNode(string localPath, MegaApi* api, MegaNode *node);
    void uploadNode(string localPath, MegaApi* api, MegaNode *node);
    void exportNode(MegaNode *n,int expireTime);
    void disableExport(MegaNode *n);
    void shareNode(MegaNode *n,string with,int level=MegaShare::ACCESS_READ);
    void disableShare(MegaNode *n, string with);
    vector<string> listpaths(string askedPath="");
    vector<string> getlistusers();

    void executecommand(vector<string> words, map<string,int> &clflags, map<string,string> &cloptions);

    bool checkNoErrors(MegaError *error, string message="");

    //doomedtodie
    void syncstat(Sync* sync);
    const char* treestatename(treestate_t ts);
    bool is_syncable(const char* name);
    int loadfile(string* name, string* data);
};

#endif // MEGACMDEXECUTER_H
