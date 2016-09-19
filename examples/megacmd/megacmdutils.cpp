#include "megacmdutils.h"

#include <sys/stat.h>


int * getNumFolderFiles(MegaNode *n, MegaApi *api){
    int * nFolderFiles = new int[2]();
//    MegaNodeList *totalnodes = api->getChildren(n,MegaApi::ORDER_DEFAULT_ASC); //sort folders first
    MegaNodeList *totalnodes = api->getChildren(n);
    for (int i = 0; i < totalnodes->size(); i++)
    {
        if (totalnodes->get(i)->getType() == MegaNode::TYPE_FILE)
        {
//            nFolderFiles[1] = totalnodes->size()-i; //found first file
//            break;
            nFolderFiles[1]++;
        }
        else
        {
            nFolderFiles[0]++; //folder
        }
    }

    int nfolders = nFolderFiles[0];
    for (int i = 0; i < nfolders; i++)
    {
        int * nFolderFilesSub = getNumFolderFiles(totalnodes->get(i), api);

        nFolderFiles[0] += nFolderFilesSub[0];
        nFolderFiles[1] += nFolderFilesSub[1];
        delete []nFolderFilesSub;
    }

    delete totalnodes;
    return nFolderFiles;
}

string getUserInSharedNode(MegaNode *n, MegaApi *api)
{
    MegaShareList * msl = api->getInSharesList();
    for (int i = 0; i < msl->size(); i++)
    {
        MegaShare *share = msl->get(i);

        if (share->getNodeHandle() == n->getHandle())
        {
            string suser = share->getUser();
            delete ( msl );
            return suser;
        }
    }

    delete ( msl );
    return "";
}


const char* getAccessLevelStr(int level){
    switch (level)
    {
        case MegaShare::ACCESS_UNKNOWN:
            return "unknown access";

            break;

        case MegaShare::ACCESS_READ:
            return "read access";

            break;

        case MegaShare::ACCESS_READWRITE:
            return "read/write access";

            break;

        case MegaShare::ACCESS_FULL:
            return "full access";

            break;

        case MegaShare::ACCESS_OWNER:
            return "owner access";

            break;
    }
    return "undefined";
}

const char* getSyncStateStr(int state){
    switch (state)
    {
        case 0:
            return "NONE";

            break;

        case MegaApi::STATE_SYNCED:
            return "Synced";

            break;

        case MegaApi::STATE_PENDING:
            return "Pending";

            break;

        case MegaApi::STATE_SYNCING:
            return "Syncing";

            break;

        case MegaApi::STATE_IGNORED:
            return "Ignored";

            break;
    }
    return "undefined";
}

string visibilityToString(int visibility)
{
    if (visibility == MegaUser::VISIBILITY_VISIBLE)
    {
        return "visible";
    }
    if (visibility == MegaUser::VISIBILITY_HIDDEN)
    {
        return "hidden";
    }
    if (visibility == MegaUser::VISIBILITY_UNKNOWN)
    {
        return "unkown visibility";
    }
    if (visibility == MegaUser::VISIBILITY_INACTIVE)
    {
        return "inactive";
    }
    if (visibility == MegaUser::VISIBILITY_BLOCKED)
    {
        return "blocked";
    }
    return "undefined visibility";
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
    if (e)
    {
        return errorstring(e->getErrorCode());
    }
    return "NullError";
}

bool isFolder(string path) //TODO: move to MegaFileSystemAccess
{
    struct stat path_stat;
    stat(path.c_str(), &path_stat);
    return S_ISDIR(path_stat.st_mode);
}

int getLinkType(string link)
{
    size_t posHash = link.find_first_of("#");
    if (( posHash == string::npos ) || !( posHash + 1 < link.length()))
    {
        return MegaNode::TYPE_UNKNOWN;
    }
    if (link.at(posHash + 1) == 'F')
    {
        return MegaNode::TYPE_FOLDER;
    }
    return MegaNode::TYPE_FILE;
}

bool isPublicLink(string link){
    if (( link.find_first_of("http") == 0 ) && ( link.find_first_of("#") != string::npos ))
    {
        return true;
    }
    return false;
}

bool isRegularFile(string path)
{
    struct stat path_stat;
    stat(path.c_str(), &path_stat);
    return S_ISREG(path_stat.st_mode);
}

bool pathExits(string path){ //TODO: move to MegaFileSystemAccess
//    return access( path, F_OK ) != -1 ;
    struct stat path_stat;
    int ret = stat(path.c_str(), &path_stat);
    return ret == 0;
}

string getCurrentLocalPath(){ //TODO: move all this into PosixFileSystemAccess
    char cCurrentPath[FILENAME_MAX];
    if (!getcwd(cCurrentPath, sizeof( cCurrentPath )))
    {
        LOG_err << "Couldn't read cwd";
        return "";
    }

    return string(cCurrentPath);
}

string expanseLocalPath(string path){ //TODO: posix dependent!
    ostringstream os;
    if (path.at(0) == '/')
    {
        return path;
    }
    else
    {
        os << getCurrentLocalPath() << "/" << path;
        return os.str();
    }
}


bool hasWildCards(string &what)
{
    return what.find('*') != string::npos || what.find('?') != string::npos; // || what.find('/')!=string::npos
}

std::string getReadableTime(const time_t rawtime)
{
    struct tm * dt;
    char buffer [40];
    dt = localtime(&rawtime);
    strftime(buffer, sizeof( buffer ), "%a, %d %b %Y %T %z", dt); // Folloging RFC 2822 (as in date -R)
    return std::string(buffer);
}

time_t getTimeStampAfter(time_t initial, string timestring)
{
    char *buffer = new char[timestring.size() + 1];
    strcpy(buffer, timestring.c_str());

    time_t days = 0, hours = 0, minutes = 0, seconds = 0, months = 0, years = 0;

    char * ptr = buffer;
    char * last = buffer;
    while (*ptr != '\0')
    {
        if (( *ptr < '0' ) || ( *ptr > '9' ))
        {
            switch (*ptr)
            {
                case 'd':
                    *ptr = '\0';
                    days = atoi(last);
                    break;

                case 'h':
                    *ptr = '\0';
                    hours = atoi(last);
                    break;

                case 'M':
                    *ptr = '\0';
                    minutes = atoi(last);
                    break;

                case 's':
                    *ptr = '\0';
                    seconds = atoi(last);
                    break;

                case 'm':
                    *ptr = '\0';
                    months = atoi(last);
                    break;

                case 'y':
                    *ptr = '\0';
                    years = atoi(last);
                    break;

                default:
                {
                    delete[] buffer;
                    return -1;
                }
            }
            last = ptr + 1;
        }
        ptr++;
    }

    struct tm * dt;
    dt = localtime(&initial);

    dt->tm_mday += days;
    dt->tm_hour += hours;
    dt->tm_min += minutes;
    dt->tm_sec += seconds;
    dt->tm_mon += months;
    dt->tm_year += years;

    delete [] buffer;
    return mktime(dt);
}

time_t getTimeStampAfter(string timestring)
{
    time_t initial = time(NULL);
    return getTimeStampAfter(initial, timestring);
}

std::string &ltrim(std::string &s, const char &c) {
    size_t pos = s.find_first_not_of(c);
    s = s.substr(pos == string::npos ? s.length() : pos, s.length());
    return s;
}

std::string &rtrim(std::string &s, const char &c) {
    size_t pos = s.find_last_of(c);
    size_t last = pos == string::npos ? s.length() : pos;
    if (last +1 < s.length() )
    {
        if (s.at(last + 1) != c)
        {
            last = s.length();
        }
    }

    s = s.substr(0, last);
    return s;
}

bool patternMatches(const char *what, const char *pattern)
{
    //return std::regex_match (pattern, std::regex(what) ); //c++11

    // If we reach at the end of both strings, we are done
    if (( *pattern == '\0' ) && ( *what == '\0' ))
    {
        return true;
    }

    // Make sure that the characters after '*' are present
    // in what string. This function assumes that the pattern
    // string will not contain two consecutive '*'
    if (( *pattern == '*' ) && ( *( pattern + 1 ) != '\0' ) && ( *what == '\0' ))
    {
        return false;
    }

    // If the pattern string contains '?', or current characters
    // of both strings match
    if (( *pattern == '?' ) || ( *pattern == *what ))
    {
        if (*what == '\0')
        {
            return false;
        }
        return patternMatches(what + 1, pattern + 1);
    }

    // If there is *, then there are two possibilities
    // a) We consider current character of what string
    // b) We ignore current character of what string.
    if (*pattern == '*')
    {
        return patternMatches(what, pattern + 1) || patternMatches(what + 1, pattern);
    }

    return false;
}


int getFlag(map<string, int> *flags, const char * optname)
{
    return flags->count(optname) ? ( *flags )[optname] : 0;
}

string getOption(map<string, string> *cloptions, const char * optname, string defaultValue)
{
    return cloptions->count(optname) ? ( *cloptions )[optname] : defaultValue;
}

int getintOption(map<string, string> *cloptions, const char * optname, int defaultValue)
{
    if (cloptions->count(optname))
    {
        int i;
        istringstream is(( *cloptions )[optname]);
        is >> i;
        return i;
    }
    else
    {
        return defaultValue;
    }
}
