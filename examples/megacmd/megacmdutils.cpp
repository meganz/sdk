#include "megacmdutils.h"

#include <sys/stat.h>


int * getNumFolderFiles(MegaNode *n, MegaApi *api){

    int * nFolderFiles = new int[2]();
//    MegaNodeList *totalnodes = api->getChildren(n,MegaApi::ORDER_DEFAULT_ASC); //sort folders first
    MegaNodeList *totalnodes = api->getChildren(n);
    for (int i=0; i<totalnodes->size();i++)
    {
        if (totalnodes->get(i)->getType() == MegaNode::TYPE_FILE)
        {
//            nFolderFiles[1] = totalnodes->size()-i; //found first file
//            break;
            nFolderFiles[1]++;
        }
        else
            nFolderFiles[0]++; //folder
    }
    int nfolders = nFolderFiles[0];
    for (int i=0; i<nfolders;i++)
    {
        int * nFolderFilesSub = getNumFolderFiles(totalnodes->get(i),api);

        nFolderFiles[0]+=  nFolderFilesSub[0];
        nFolderFiles[1]+=  nFolderFilesSub[1];
        delete []nFolderFilesSub;

    }
    delete totalnodes;
    return nFolderFiles;
}

string getUserInSharedNode(MegaNode *n, MegaApi *api)
{
    MegaShareList * msl = api->getInSharesList();
    for (int i=0;i<msl->size();i++)
    {
        MegaShare *share = msl->get(i);

        if (share->getNodeHandle() == n->getHandle())
        {
            string suser = share->getUser();
            delete (msl);
            return suser;
        }
    }
    delete (msl);
    return "";
}



const char* getAccessLevelStr(int level){
    switch (level){
        case MegaShare::ACCESS_UNKNOWN: return "unknown access"; break;
        case MegaShare::ACCESS_READ: return "read access"; break;
        case MegaShare::ACCESS_READWRITE: return "read/write access"; break;
        case MegaShare::ACCESS_FULL: return "full access"; break;
        case MegaShare::ACCESS_OWNER: return "owner access"; break;
    };
    return "undefined";
}

const char* getSyncStateStr(int state){
    switch (state){
    case 0: return "NONE"; break;
    case MegaApi::STATE_SYNCED: return "Synced"; break;
    case MegaApi::STATE_PENDING: return "Pending"; break;
    case MegaApi::STATE_SYNCING: return "Syncing"; break;
    case MegaApi::STATE_IGNORED: return "Ignored"; break;
    };
    return "undefined";
}

const char* errorstring(int e)
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
        default:
            return "Unknown error";
    }
}

const char * getErrorCodeStr(MegaError *e)
{
    if (e) return errorstring(e->getErrorCode());
    return "NullError";
}



bool ifPathAFolder(const char * path){
    struct stat s;
    if( stat(path,&s) == 0 )
    {
        if( s.st_mode & S_IFDIR )
        {
            return true;
        }
        else
        {
            LOG_verbose << "Path is not a folder: " << path ;
        }
    }
    else
    {
        LOG_verbose << "Path not found: " << path;
    }
    return false;
}
