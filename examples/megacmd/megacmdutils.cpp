/**
 * @file examples/megacmd/megacmd.cpp
 * @brief MegaCMD: Auxiliary methods
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

#include "megacmdutils.h"

#include <sys/stat.h>
#ifdef USE_PCRE
#include <pcrecpp.h>
#elif __cplusplus >= 201103L
#include <regex>
#endif


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


const char* getAttrStr(int attr){
    switch (attr)
    {

    case MegaApi::USER_ATTR_AVATAR:
        return "avatar";



    case MegaApi::USER_ATTR_FIRSTNAME:
        return "firstname";



    case MegaApi::USER_ATTR_LASTNAME:
        return "lastname";



    case MegaApi::USER_ATTR_AUTHRING:
        return "authring";



    case MegaApi::USER_ATTR_LAST_INTERACTION:
        return "lastinteraction";



    case MegaApi::USER_ATTR_ED25519_PUBLIC_KEY:
        return "ed25519";



    case MegaApi::USER_ATTR_CU25519_PUBLIC_KEY:
        return "cu25519";



    case MegaApi::USER_ATTR_KEYRING:
        return "keyring";



    case MegaApi::USER_ATTR_SIG_RSA_PUBLIC_KEY:
        return "rsa";



    case MegaApi::USER_ATTR_SIG_CU255_PUBLIC_KEY:
        return "cu255";


  }
    return "undefined";
}

int getAttrNum(const char* attr){
    if (!strcmp(attr,"avatar")) return MegaApi:: USER_ATTR_AVATAR;
    if (!strcmp(attr,"firstname")) return MegaApi:: USER_ATTR_FIRSTNAME;
    if (!strcmp(attr,"lastname")) return MegaApi:: USER_ATTR_LASTNAME;
    if (!strcmp(attr,"authring")) return MegaApi:: USER_ATTR_AUTHRING;
    if (!strcmp(attr,"lastinteraction")) return MegaApi:: USER_ATTR_LAST_INTERACTION;
    if (!strcmp(attr,"ed25519")) return MegaApi:: USER_ATTR_ED25519_PUBLIC_KEY;
    if (!strcmp(attr,"cu25519")) return MegaApi:: USER_ATTR_CU25519_PUBLIC_KEY;
    if (!strcmp(attr,"keyring")) return MegaApi:: USER_ATTR_KEYRING;
    if (!strcmp(attr,"rsa")) return MegaApi:: USER_ATTR_SIG_RSA_PUBLIC_KEY;
    if (!strcmp(attr,"cu255")) return MegaApi:: USER_ATTR_SIG_CU255_PUBLIC_KEY;
    return atoi(attr);
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

const char * getLogLevelStr(int loglevel)
{
    switch (loglevel)
    {
    case MegaApi::LOG_LEVEL_FATAL:
        return "FATAL";
        break;
    case MegaApi::LOG_LEVEL_ERROR:
        return "ERROR";
        break;
    case MegaApi::LOG_LEVEL_WARNING:
        return "WARNING";
        break;
    case MegaApi::LOG_LEVEL_INFO:
        return "INFO";
        break;
    case MegaApi::LOG_LEVEL_DEBUG:
        return "DEBUG";
        break;
    case MegaApi::LOG_LEVEL_MAX:
        return "VERBOSE";
        break;
    default:
        return "UNKNOWN";
        break;
    }
}

int getLogLevelNum(const char* level){
    if (!strcmp(level,"FATAL")) return MegaApi:: LOG_LEVEL_FATAL;
    if (!strcmp(level,"ERROR")) return MegaApi:: LOG_LEVEL_ERROR;
    if (!strcmp(level,"WARNING")) return MegaApi:: LOG_LEVEL_WARNING;
    if (!strcmp(level,"INFO")) return MegaApi:: LOG_LEVEL_INFO;
    if (!strcmp(level,"DEBUG")) return MegaApi:: LOG_LEVEL_DEBUG;
    if (!strcmp(level,"VERBOSE")) return MegaApi:: LOG_LEVEL_MAX;
    return atoi(level);
}

bool isFolder(string path) //TODO: move to MegaFileSystemAccess
{
    struct stat path_stat;
    stat(path.c_str(), &path_stat);
    return S_ISDIR(path_stat.st_mode);
}

bool canWrite(string path)
{
    if (access(path.c_str(), W_OK) == 0)
        return true;
    return false;
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

vector<string> getlistOfWords(char *ptr)
{
    vector<string> words;

    char* wptr;

    // split line into words with quoting and escaping
    for (;; )
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

            for (;; )
            {
                if (( *ptr == '"' ) || ( *ptr == '\\' ) || !*ptr)
                {
                    words[words.size() - 1].append(wptr, ptr - wptr);

                    if (!*ptr || ( *ptr++ == '"' ))
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

            while ((unsigned char)*ptr > ' ')
            {
                if (*ptr == '"')
                {
                    while (*++ptr != '"' && *ptr != '\0')
                    { }
                }
                ptr++;
            }

            words.push_back(string(wptr, ptr - wptr));
        }
    }

    return words;
}

bool stringcontained(const char * s, vector<string> list){
    for (int i = 0; i < (int) list.size(); i++)
    {
        if (list[i] == s)
        {
            return true;
        }
    }

    return false;
}

char * dupstr(char* s) {
    char *r;

    r = (char*)malloc(sizeof( char ) * ( strlen(s) + 1 ));
    strcpy(r, s);
    return( r );
}


bool replace(std::string& str, const std::string& from, const std::string& to) {
    size_t start_pos = str.find(from);
    if(start_pos == std::string::npos)
        return false;
    str.replace(start_pos, from.length(), to);
    return true;
}

void replaceAll(std::string& str, const std::string& from, const std::string& to) {
    if(from.empty())
        return;
    size_t start_pos = 0;
    while((start_pos = str.find(from, start_pos)) != std::string::npos) {
        str.replace(start_pos, from.length(), to);
        start_pos += from.length();
    }
}

bool isRegExp(string what)
{
#ifdef USE_PCRE
    if (what == "." || what == "..") return false;

    string s = pcrecpp::RE::QuoteMeta(what);
    string ns=s;
    replaceAll(ns,"\\\\\\","\\");
    bool isregex = strcmp(what.c_str(),ns.c_str());
    return isregex;
#elif __cplusplus >= 201103L
    //TODO??
#endif
    return hasWildCards(what);
}

string unquote(string what)
{
#ifdef USE_PCRE
    if (what == "." || what == "..") return what;
    string s = pcrecpp::RE::QuoteMeta(what.c_str());
    string ns=s;
    replaceAll(ns,"\\\\\\","\\");
    return ns;
#endif
    return what;
}

bool patternMatches(const char *what, const char *pattern)
{

#ifdef USE_PCRE
    pcrecpp::RE re(pattern);

    if (!re.error().length() > 0) {
        bool toret = re.FullMatch(what);

        return toret;
    }
    else
    {
       LOG_verbose << "Invalid PCRE regex: " << re.error();
    }
#elif __cplusplus >= 201103L
    try
    {
        return std::regex_match (what, std::regex(pattern) );
    }
    catch (std::regex_error e)
    {
        LOG_warn << "Couldn't compile regex: " << pattern;
    }
#endif

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

bool setOptionsAndFlags(map<string, string> *opts, map<string, int> *flags, vector<string> *ws, set<string> vvalidOptions, bool global)
{
    bool discarded = false;

    for (std::vector<string>::iterator it = ws->begin(); it != ws->end(); )
    {
        /* std::cout << *it; ... */
        string w = ( string ) * it;
        if (w.length() && ( w.at(0) == '-' )) //begins with "-"
        {
            if (( w.length() > 1 ) && ( w.at(1) != '-' ))  //single character flags!
            {
                for (uint i = 1; i < w.length(); i++)
                {
                    string optname = w.substr(i, 1);
                    if (vvalidOptions.find(optname) != vvalidOptions.end())
                    {
                        ( *flags )[optname] = ( flags->count(optname) ? ( *flags )[optname] : 0 ) + 1;
                    }
                    else
                    {
                        LOG_err << "Invalid argument: " << optname;
                        discarded = true;
                    }
                }
            }
            else if (w.find_first_of("=") == std::string::npos) //flag
            {
                string optname = ltrim(w, '-');
                if (vvalidOptions.find(optname) != vvalidOptions.end())
                {
                    ( *flags )[optname] = ( flags->count(optname) ? ( *flags )[optname] : 0 ) + 1;
                }
                else
                {
                    LOG_err << "Invalid argument: " << optname;
                    discarded = true;
                }
            }
            else //option=value
            {
                string cleared = ltrim(w, '-');
                size_t p = cleared.find_first_of("=");
                string optname = cleared.substr(0, p);
                if (vvalidOptions.find(optname) != vvalidOptions.end())
                {
                    string value = cleared.substr(p + 1);

                    value = rtrim(ltrim(value, '"'), '"');
                    ( *opts )[optname] = value;
                }
                else
                {
                    LOG_err << "Invalid argument: " << optname;
                    discarded = true;
                }
            }
            it = ws->erase(it);
        }
        else //not an option/flag
        {
            if (global)
            {
                return discarded; //leave the others
            }
            ++it;
        }
    }

    return discarded;
}

string sizeToText(long long totalSize, bool equalizeUnitsLength, bool humanreadable)
{
    ostringstream os;
    os.precision(3);
    if (humanreadable)
    {
        double reducedSize = (totalSize > 1048576*2?totalSize/1048576.0:(totalSize>1024*2?totalSize/1024.0:totalSize));
        os << reducedSize;
        os << (totalSize > 1048576*2?" MB":(totalSize>1024*2?" KB":(equalizeUnitsLength?"  B":" B")));
    }
    else
    {
        os << totalSize;
    }

    return os.str();
}
