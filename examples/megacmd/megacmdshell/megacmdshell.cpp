/**
 * @file examples/megacmd/megacmdshell.cpp
 * @brief MegaCMD: Interactive CLI and service application
 * This is the shell application
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

#include "megacmdshell.h"
#include "megacmdshellcommunications.h"


//#include "megacmd.h"

//#include "megacmdsandbox.h"
//#include "megacmdexecuter.h"
//#include "megacmdutils.h"
//#include "configurationmanager.h"
//#include "megacmdlogger.h"
//#include "comunicationsmanager.h"
//#include "listeners.h"

//#include "megacmdplatform.h"
//#include "megacmdversion.h"

#define USE_VARARGS
#define PREFER_STDARG

#include <readline/readline.h>
#include <readline/history.h>
#include <iomanip>
#include <string>
#include <set>
#include <map>
#include <vector>
#include <sstream>
#include <algorithm>

//TODO: check includes ok in windows:
#include <sys/types.h>
#include <unistd.h>
#include <termios.h>


#ifndef _WIN32
#include <signal.h>
#endif


using namespace std;


//TODO: move utils functions somewhere else (common with megacmdserver?)


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
        else if (*ptr == '\'') // quoted arg / regular arg
        {
            ptr++;
            wptr = ptr;
            words.push_back(string());

            for (;; )
            {
                if (( *ptr == '\'' ) || ( *ptr == '\\' ) || !*ptr)
                {
                    words[words.size() - 1].append(wptr, ptr - wptr);

                    if (!*ptr || ( *ptr++ == '\'' ))
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

            char *prev = ptr;
            //while ((unsigned char)*ptr > ' ')
            while ((*ptr != '\0') && !(*ptr ==' ' && *prev !='\\'))
            {
                if (*ptr == '"')
                {
                    while (*++ptr != '"' && *ptr != '\0')
                    { }
                }
                prev=ptr;
                ptr++;
            }

            words.push_back(string(wptr, ptr - wptr));
        }
    }

    return words;
}

bool stringcontained(const char * s, vector<string> list)
{
    for (int i = 0; i < (int)list.size(); i++)
    {
        if (list[i] == s)
        {
            return true;
        }
    }

    return false;
}
char * dupstr(char* s)
{
    char *r;

    r = (char*)malloc(sizeof( char ) * ( strlen(s) + 1 ));
    strcpy(r, s);
    return( r );
}


bool replace(std::string& str, const std::string& from, const std::string& to)
{
    size_t start_pos = str.find(from);
    if (start_pos == std::string::npos)
    {
        return false;
    }
    str.replace(start_pos, from.length(), to);
    return true;
}

void replaceAll(std::string& str, const std::string& from, const std::string& to)
{
    if (from.empty())
    {
        return;
    }
    size_t start_pos = 0;
    while (( start_pos = str.find(from, start_pos)) != std::string::npos)
    {
        str.replace(start_pos, from.length(), to);
        start_pos += to.length();
    }
}

bool hasWildCards(string &what)
{
    return what.find('*') != string::npos || what.find('?') != string::npos;
}

bool isRegExp(string what)
{
#ifdef USE_PCRE
    if (( what == "." ) || ( what == ".." ) || ( what == "/" ))
    {
        return false;
    }

    while (true){
        if (what.find("./") == 0)
        {
            what=what.substr(2);
        }
        else if(what.find("../") == 0)
        {
            what=what.substr(3);
        }
        else if(what.size()>=3 && (what.find("/..") == what.size()-3))
        {
            what=what.substr(0,what.size()-3);
        }
        else if(what.size()>=2 && (what.find("/.") == what.size()-2))
        {
            what=what.substr(0,what.size()-2);
        }
        else if(what.size()>=2 && (what.find("/.") == what.size()-2))
        {
            what=what.substr(0,what.size()-2);
        }
        else
        {
            break;
        }
    }
    replaceAll(what, "/../", "/");
    replaceAll(what, "/./", "/");
    replaceAll(what, "/", "");

    string s = pcrecpp::RE::QuoteMeta(what);
    string ns = s;
    replaceAll(ns, "\\\\\\", "\\");
    bool isregex = strcmp(what.c_str(), ns.c_str());
    return isregex;

#elif __cplusplus >= 201103L
    //TODO??
#endif
    return hasWildCards(what);
}


string getCurrentThreadLine() //TODO: rename to sth more sensefull
{
    char *saved_line = rl_copy_text(0, rl_point);
    string toret(saved_line);
    free(saved_line);
    return toret;

}


/// end utily functions




#ifdef _WIN32
// convert UTF-8 to Windows Unicode wstring
void stringtolocalw(const char* path, std::wstring* local)
{
    // make space for the worst case
    local->resize((strlen(path) + 1) * sizeof(wchar_t));

    int wchars_num = MultiByteToWideChar(CP_UTF8, 0, path,-1, NULL,0);
    local->resize(wchars_num);

    int len = MultiByteToWideChar(CP_UTF8, 0, path,-1, (wchar_t*)local->data(), wchars_num);

    if (len)
    {
        local->resize(len-1);
    }
    else
    {
        local->clear();
    }
}

//override << operators for wostream for string and const char *

std::wostream & operator<< ( std::wostream & ostr, std::string const & str )
{
    std::wstring toout;
    stringtolocalw(str.c_str(),&toout);
    ostr << toout;

return ( ostr );
}

std::wostream & operator<< ( std::wostream & ostr, const char * str )
{
    std::wstring toout;
    stringtolocalw(str,&toout);
    ostr << toout;
    return ( ostr );
}

//override for the log. This is required for compiling, otherwise SimpleLog won't compile. FIXME
std::ostringstream & operator<< ( std::ostringstream & ostr, std::wstring const &str)
{
    //TODO: localtostring
    //std::wstring toout;
    //stringtolocalw(str,&toout);
    //ostr << toout;
    return ( ostr );
}


#endif

//#ifdef _WIN32
//#include "comunicationsmanagerportsockets.h"
//#define COMUNICATIONMANAGER ComunicationsManagerPortSockets
//#else
//#include "comunicationsmanagerfilesockets.h"
//#define COMUNICATIONMANAGER ComunicationsManagerFileSockets
//#include <signal.h>
//#endif

//using namespace mega;

//MegaCmdExecuter *cmdexecuter;
//MegaCmdSandbox *sandboxCMD;

//MegaSemaphore semaphoreClients; //to limit max parallel petitions

//MegaApi *api;

////api objects for folderlinks
//std::queue<MegaApi *> apiFolders;
//std::vector<MegaApi *> occupiedapiFolders;
//MegaSemaphore semaphoreapiFolders;
//MegaMutex mutexapiFolders;

//MegaCMDLogger *loggerCMD;

//MegaMutex mutexEndedPetitionThreads;
//std::vector<MegaThread *> petitionThreads;
//std::vector<MegaThread *> endedPetitionThreads;


////Comunications Manager
//ComunicationsManager * cm;

//// global listener
//MegaCmdGlobalListener* megaCmdGlobalListener;

//MegaCmdMegaListener* megaCmdMegaListener;

//bool loginInAtStartup = false;

string validGlobalParameters[] = {"v", "help"};

string alocalremotefolderpatterncommands [] = {"sync"};
vector<string> localremotefolderpatterncommands(alocalremotefolderpatterncommands, alocalremotefolderpatterncommands + sizeof alocalremotefolderpatterncommands / sizeof alocalremotefolderpatterncommands[0]);

string aremotepatterncommands[] = {"export", "find", "attr"};
vector<string> remotepatterncommands(aremotepatterncommands, aremotepatterncommands + sizeof aremotepatterncommands / sizeof aremotepatterncommands[0]);

string aremotefolderspatterncommands[] = {"cd", "share"};
vector<string> remotefolderspatterncommands(aremotefolderspatterncommands, aremotefolderspatterncommands + sizeof aremotefolderspatterncommands / sizeof aremotefolderspatterncommands[0]);

string amultipleremotepatterncommands[] = {"ls", "mkdir", "rm", "du"};
vector<string> multipleremotepatterncommands(amultipleremotepatterncommands, amultipleremotepatterncommands + sizeof amultipleremotepatterncommands / sizeof amultipleremotepatterncommands[0]);

string aremoteremotepatterncommands[] = {"mv", "cp"};
vector<string> remoteremotepatterncommands(aremoteremotepatterncommands, aremoteremotepatterncommands + sizeof aremoteremotepatterncommands / sizeof aremoteremotepatterncommands[0]);

string aremotelocalpatterncommands[] = {"get", "thumbnail", "preview"};
vector<string> remotelocalpatterncommands(aremotelocalpatterncommands, aremotelocalpatterncommands + sizeof aremotelocalpatterncommands / sizeof aremotelocalpatterncommands[0]);

string alocalpatterncommands [] = {"lcd"};
vector<string> localpatterncommands(alocalpatterncommands, alocalpatterncommands + sizeof alocalpatterncommands / sizeof alocalpatterncommands[0]);

string aemailpatterncommands [] = {"invite", "signup", "ipc", "users"};
vector<string> emailpatterncommands(aemailpatterncommands, aemailpatterncommands + sizeof aemailpatterncommands / sizeof aemailpatterncommands[0]);

string avalidCommands [] = { "login", "signup", "confirm", "session", "mount", "ls", "cd", "log", "debug", "pwd", "lcd", "lpwd", "import",
                             "put", "get", "attr", "userattr", "mkdir", "rm", "du", "mv", "cp", "sync", "export", "share", "invite", "ipc",
                             "showpcr", "users", "speedlimit", "killsession", "whoami", "help", "passwd", "reload", "logout", "version", "quit",
                             "history", "thumbnail", "preview", "find", "completion", "clear", "https", "transfers"
#ifdef _WIN32
                             ,"unicode"
#endif
                           };
vector<string> validCommands(avalidCommands, avalidCommands + sizeof avalidCommands / sizeof avalidCommands[0]);


// password change-related state information
string oldpasswd;
string newpasswd;

bool doExit = false;
bool consoleFailed = false;

bool handlerinstalled = false;

static char dynamicprompt[128];

static char* line;

static prompttype prompt = COMMAND;

static char pw_buf[256];
static int pw_buf_pos;

// communications with megacmdserver:
MegaCmdShellCommunications *comms;


//// local console
//Console* console; //TODO: use MEGA console // repeat sources??? // include them as in the sdk.pri

//MegaMutex mutexHistory;

//map<int, string> threadline;

void printWelcomeMsg();

//string getCurrentThreadLine()
//{
//    uint64_t currentThread = MegaThread::currentThreadId();
//    if (threadline.find(currentThread) == threadline.end())
//    {
//        char *saved_line = rl_copy_text(0, rl_point);
//        string toret(saved_line);
//        free(saved_line);
//        return toret;
//    }
//    else
//    {
//        return threadline[currentThread];
//    }
//}

//void setCurrentThreadLine(string s)
//{
//    threadline[MegaThread::currentThreadId()] = s;
//}

//void setCurrentThreadLine(const vector<string>& vec)
//{
//   setCurrentThreadLine(joinStrings(vec));
//}

void sigint_handler(int signum)
{
//    LOG_verbose << "Received signal: " << signum;

//    if (loginInAtStartup)
//    {
//        exit(-2);
//    }

    if (prompt != COMMAND)
    {
        setprompt(COMMAND);
    }

    // reset position and print prompt
    rl_replace_line("", 0); //clean contents of actual command
    rl_crlf(); //move to nextline

    if (RL_ISSTATE(RL_STATE_ISEARCH) || RL_ISSTATE(RL_STATE_ISEARCH) || RL_ISSTATE(RL_STATE_ISEARCH))
    {
        RL_UNSETSTATE(RL_STATE_ISEARCH);
        RL_UNSETSTATE(RL_STATE_NSEARCH);
        RL_UNSETSTATE( RL_STATE_SEARCH);
        history_set_pos(history_length);
        rl_restore_prompt(); // readline has stored it when searching
    }
    else
    {
        rl_reset_line_state();
    }
    rl_redisplay();
}

#ifdef _WIN32
BOOL CtrlHandler( DWORD fdwCtrlType )
{
  LOG_verbose << "Reached CtrlHandler: " << fdwCtrlType;

  switch( fdwCtrlType )
  {
    // Handle the CTRL-C signal.
    case CTRL_C_EVENT:
       sigint_handler((int)fdwCtrlType);
      return( TRUE );

    default:
      return FALSE;
  }
}
#endif

prompttype getprompt()
{
    return prompt;
}

void setprompt(prompttype p, string arg)
{
    prompt = p;

    if (p == COMMAND)
    {
//        console->setecho(true); //TODO: use console
    }
    else
    {
        pw_buf_pos = 0;
        if (arg.size())
        {
            OUTSTREAM << arg << flush;
        }
        else
        {
            OUTSTREAM << prompts[p] << flush;
        }

//        console->setecho(false);//TODO: use console
    }
}


// readline callback - exit if EOF, add to history unless password
static void store_line(char* l)
{
    if (!l)
    {
#ifndef _WIN32 // to prevent exit with Supr key
        doExit = true;
        rl_set_prompt("(CTRL+D) Exiting ...\n");
        if (comms->serverinitiatedfromshell)
        {
            comms->executeCommand("exit");
        }
#endif
        return;
    }

    if (*l && ( prompt == COMMAND ))
    {
//        mutexHistory.lock(); //TODO: use mutex!
        add_history(l);
//        mutexHistory.unlock();
    }

    line = l;
}

void changeprompt(const char *newprompt, bool redisplay)
{
    strncpy(dynamicprompt, newprompt, sizeof( dynamicprompt ));

    if (redisplay)
    {
        rl_crlf();
//        rl_redisplay();
//        rl_forced_update_display(); //this is not enough.  The problem is that the prompt is actually printed when calling rl_callback_handeler_install or after an enter
        rl_callback_handler_install(*dynamicprompt ? dynamicprompt : prompts[COMMAND], store_line);
        handlerinstalled = true;

        //TODO: problems:
        // - concurrency issues?
        // - current command being written is lost here //play with save_line. Again, deal with concurrency
        // - whenever I fix the hang when I was the one that triggered the state change. If I was in a new line and handler_install was already called none of this should be
        //    required. But what if the external communication was faster or slower than me. Think and test

    }
}

void insertValidParamsPerCommand(set<string> *validParams, string thecommand, set<string> *validOptValues = NULL)
{
    if (!validOptValues)
    {
        validOptValues = validParams;
    }
    if ("ls" == thecommand)
    {
        validParams->insert("R");
        validParams->insert("r");
        validParams->insert("l");
    }
    else if ("du" == thecommand)
    {
        validParams->insert("h");
    }
    else if ("help" == thecommand)
    {
        validParams->insert("f");
        validParams->insert("non-interactive");
        validParams->insert("upgrade");
    }
    else if ("version" == thecommand)
    {
        validParams->insert("l");
        validParams->insert("c");
    }
    else if ("rm" == thecommand)
    {
        validParams->insert("r");
        validParams->insert("f");
    }
    else if ("speedlimit" == thecommand)
    {
        validParams->insert("u");
        validParams->insert("d");
        validParams->insert("h");
    }
    else if ("whoami" == thecommand)
    {
        validParams->insert("l");
    }
    else if ("log" == thecommand)
    {
        validParams->insert("c");
        validParams->insert("s");
    }
    else if ("sync" == thecommand)
    {
        validParams->insert("d");
        validParams->insert("s");
    }
    else if ("export" == thecommand)
    {
        validParams->insert("a");
        validParams->insert("d");
        validOptValues->insert("expire");
    }
    else if ("share" == thecommand)
    {
        validParams->insert("a");
        validParams->insert("d");
        validParams->insert("p");
        validOptValues->insert("with");
        validOptValues->insert("level");
        validOptValues->insert("personal-representation");
    }
    else if ("find" == thecommand)
    {
        validOptValues->insert("pattern");
        validOptValues->insert("l");
    }
    else if ("mkdir" == thecommand)
    {
        validParams->insert("p");
    }
    else if ("users" == thecommand)
    {
        validParams->insert("s");
        validParams->insert("h");
        validParams->insert("d");
        validParams->insert("n");
    }
    else if ("killsession" == thecommand)
    {
        validParams->insert("a");
    }
    else if ("invite" == thecommand)
    {
        validParams->insert("d");
        validParams->insert("r");
        validOptValues->insert("message");
    }
    else if ("signup" == thecommand)
    {
        validParams->insert("name");
    }
    else if ("logout" == thecommand)
    {
        validParams->insert("keep-session");
    }
    else if ("attr" == thecommand)
    {
        validParams->insert("d");
        validParams->insert("s");
    }
    else if ("userattr" == thecommand)
    {
        validOptValues->insert("user");
        validParams->insert("s");
    }
    else if ("ipc" == thecommand)
    {
        validParams->insert("a");
        validParams->insert("d");
        validParams->insert("i");
    }
    else if ("thumbnail" == thecommand)
    {
        validParams->insert("s");
    }
    else if ("preview" == thecommand)
    {
        validParams->insert("s");
    }
    else if ("put" == thecommand)
    {
        validParams->insert("c");
        validParams->insert("q");
        validParams->insert("ignore-quota-warn");
    }
    else if ("get" == thecommand)
    {
        validParams->insert("m");
        validParams->insert("q");
        validParams->insert("ignore-quota-warn");
    }
    else if ("transfers" == thecommand)
    {
        validParams->insert("show-completed");
        validParams->insert("only-uploads");
        validParams->insert("only-completed");
        validParams->insert("only-downloads");
        validParams->insert("show-syncs");
        validParams->insert("c");
        validParams->insert("a");
        validParams->insert("p");
        validParams->insert("r");
        validOptValues->insert("limit");
        validOptValues->insert("path-display-size");
    }
}

void escapeEspace(string &orig)
{
    replaceAll(orig," ", "\\ ");
}

void unescapeEspace(string &orig)
{
    replaceAll(orig,"\\ ", " ");
}


char* empty_completion(const char* text, int state)
{
    // we offer 2 different options so that it doesn't complete (no space is inserted)
    if (state == 0)
    {
        return strdup(" ");
    }
    if (state == 1)
    {
        return strdup(text);
    }
    return NULL;
}

//char* generic_completion(const char* text, int state, vector<string> validOptions)
//{
//    static size_t list_index, len;
//    static bool foundone;
//    string name;
//    if (!validOptions.size()) // no matches
//    {
//        return empty_completion(text,state); //dont fall back to filenames
//    }
//    if (!state)
//    {
//        list_index = 0;
//        foundone = false;
//        len = strlen(text);
//    }
//    while (list_index < validOptions.size())
//    {
//        name = validOptions.at(list_index);
//        if (!rl_completion_quote_character) {
////        if (!rl_completion_quote_character && interactiveThread()) {
//            escapeEspace(name);
//        }

//        list_index++;

//        if (!( strcmp(text, "")) || (( name.size() >= len ) && ( strlen(text) >= len ) && ( name.find(text) == 0 )))
//        {
//            if (name.size() && (( name.at(name.size() - 1) == '=' ) || ( name.at(name.size() - 1) == '/' )))
//            {
//                rl_completion_suppress_append = 1;
//            }
//            foundone = true;
//            return dupstr((char*)name.c_str());
//        }
//    }

//    if (!foundone)
//    {
//        return empty_completion(text,state); //dont fall back to filenames
//    }

//    return((char*)NULL );
//}

//char* commands_completion(const char* text, int state)
//{
//    return generic_completion(text, state, validCommands);
//}

//char* local_completion(const char* text, int state)
//{
//    return((char*)NULL );  //matches will be NULL: readline will use local completion
//}

//void addGlobalFlags(set<string> *setvalidparams)
//{
//    for (size_t i = 0; i < sizeof( validGlobalParameters ) / sizeof( *validGlobalParameters ); i++)
//    {
//        setvalidparams->insert(validGlobalParameters[i]);
//    }
//}

//char * flags_completion(const char*text, int state)
//{
//    static vector<string> validparams;
//    if (state == 0)
//    {
//        validparams.clear();
//        char *saved_line = strdup(getCurrentThreadLine().c_str());
//        vector<string> words = getlistOfWords(saved_line);
//        free(saved_line);
//        if (words.size())
//        {
//            set<string> setvalidparams;
//            set<string> setvalidOptValues;
//            addGlobalFlags(&setvalidparams);

//            string thecommand = words[0];
//            insertValidParamsPerCommand(&setvalidparams, thecommand, &setvalidOptValues);
//            set<string>::iterator it;
//            for (it = setvalidparams.begin(); it != setvalidparams.end(); it++)
//            {
//                string param = *it;
//                string toinsert;

//                if (param.size() > 1)
//                {
//                    toinsert = "--" + param;
//                }
//                else
//                {
//                    toinsert = "-" + param;
//                }

//                validparams.push_back(toinsert);
//            }

//            for (it = setvalidOptValues.begin(); it != setvalidOptValues.end(); it++)
//            {
//                string param = *it;
//                string toinsert;

//                if (param.size() > 1)
//                {
//                    toinsert = "--" + param + '=';
//                }
//                else
//                {
//                    toinsert = "-" + param + '=';
//                }

//                validparams.push_back(toinsert);
//            }
//        }
//    }
//    char *toret = generic_completion(text, state, validparams);
//    return toret;
//}

//char * flags_value_completion(const char*text, int state)
//{
//    static vector<string> validValues;

//    if (state == 0)
//    {
//        validValues.clear();

//        char *saved_line = strdup(getCurrentThreadLine().c_str());
//        vector<string> words = getlistOfWords(saved_line);
//        free(saved_line);
//        if (words.size() > 1)
//        {
//            string thecommand = words[0];
//            string currentFlag = words[words.size() - 1];

//            map<string, string> cloptions;
//            map<string, int> clflags;

//            set<string> validParams;

//            insertValidParamsPerCommand(&validParams, thecommand);

//            if (setOptionsAndFlags(&cloptions, &clflags, &words, validParams, true))
//            {
//                // return invalid??
//            }

//            if (thecommand == "share")
//            {
//                if (currentFlag.find("--level=") == 0)
//                {
//                    string prefix = strncmp(text, "--level=", strlen("--level="))?"":"--level=";
//                    validValues.push_back(prefix+getShareLevelStr(MegaShare::ACCESS_UNKNOWN));
//                    validValues.push_back(prefix+getShareLevelStr(MegaShare::ACCESS_READ));
//                    validValues.push_back(prefix+getShareLevelStr(MegaShare::ACCESS_READWRITE));
//                    validValues.push_back(prefix+getShareLevelStr(MegaShare::ACCESS_FULL));
//                    validValues.push_back(prefix+getShareLevelStr(MegaShare::ACCESS_OWNER));
//                    validValues.push_back(prefix+getShareLevelStr(MegaShare::ACCESS_UNKNOWN));
//                }
//                if (currentFlag.find("--with=") == 0)
//                {
////                    validValues = cmdexecuter->getlistusers(); //TODO: this should be asked via socket
//                    string prefix = strncmp(text, "--with=", strlen("--with="))?"":"--with=";
//                    for (u_int i=0;i<validValues.size();i++)
//                    {
//                        validValues.at(i)=prefix+validValues.at(i);
//                    }
//                }
//            }
//            if (( thecommand == "userattr" ) && ( currentFlag.find("--user=") == 0 ))
//            {
////                validValues = cmdexecuter->getlistusers(); //TODO: this should be asked via socket
//                string prefix = strncmp(text, "--user=", strlen("--user="))?"":"--user=";
//                for (u_int i=0;i<validValues.size();i++)
//                {
//                    validValues.at(i)=prefix+validValues.at(i);
//                }
//            }
//        }
//    }

//    char *toret = generic_completion(text, state, validValues);
//    return toret;
//}

//void unescapeifRequired(string &what)
//{
//    if (!rl_completion_quote_character) {
//        return unescapeEspace(what);
//    }
//}

//char* remotepaths_completion(const char* text, int state)
//{
//    static vector<string> validpaths;
//    if (state == 0)
//    {
//        string wildtext(text);
//#ifdef USE_PCRE
//        wildtext += ".";
//#elif __cplusplus >= 201103L
//        wildtext += ".";
//#endif
//        wildtext += "*";

//        if (!rl_completion_quote_character) {
//            unescapeEspace(wildtext);
//        }
////        validpaths = cmdexecuter->listpaths(wildtext); //TODO: this should be asked via socket
//    }
//    return generic_completion(text, state, validpaths);
//}

//char* remotefolders_completion(const char* text, int state)
//{
//    static vector<string> validpaths;
//    if (state == 0)
//    {
//        string wildtext(text);
//#ifdef USE_PCRE
//        wildtext += ".";
//#elif __cplusplus >= 201103L
//        wildtext += ".";
//#endif
//        wildtext += "*";

////        validpaths = cmdexecuter->listpaths(wildtext, true); //TODO: this should be asked via socket
//    }
//    return generic_completion(text, state, validpaths);
//}

//char* loglevels_completion(const char* text, int state)
//{
//    static vector<string> validloglevels;
//    if (state == 0)
//    {
//        validloglevels.push_back(getLogLevelStr(MegaApi::LOG_LEVEL_FATAL));
//        validloglevels.push_back(getLogLevelStr(MegaApi::LOG_LEVEL_ERROR));
//        validloglevels.push_back(getLogLevelStr(MegaApi::LOG_LEVEL_WARNING));
//        validloglevels.push_back(getLogLevelStr(MegaApi::LOG_LEVEL_INFO));
//        validloglevels.push_back(getLogLevelStr(MegaApi::LOG_LEVEL_DEBUG));
//        validloglevels.push_back(getLogLevelStr(MegaApi::LOG_LEVEL_MAX));
//    }
//    return generic_completion(text, state, validloglevels);
//}

//char* contacts_completion(const char* text, int state)
//{
//    static vector<string> validcontacts;
//    if (state == 0)
//    {
////        validcontacts = cmdexecuter->getlistusers(); //TODO: this should be asked via socket
//    }
//    return generic_completion(text, state, validcontacts);
//}

//char* sessions_completion(const char* text, int state)
//{
//    static vector<string> validSessions;
//    if (state == 0)
//    {
////        validSessions = cmdexecuter->getsessions(); //TODO: this should be asked via socket
//    }

//    if (validSessions.size() == 0)
//    {
//        return empty_completion(text, state);
//    }

//    return generic_completion(text, state, validSessions);
//}

//char* nodeattrs_completion(const char* text, int state)
//{
//    static vector<string> validAttrs;
//    if (state == 0)
//    {
//        validAttrs.clear();
//        char *saved_line = strdup(getCurrentThreadLine().c_str());
//        vector<string> words = getlistOfWords(saved_line);
//        free(saved_line);
//        if (words.size() > 1)
//        {
////            validAttrs = cmdexecuter->getNodeAttrs(words[1]); //TODO: this should be asked via socket
//        }
//    }

//    if (validAttrs.size() == 0)
//    {
//        return empty_completion(text, state);
//    }

//    return generic_completion(text, state, validAttrs);
//}

//char* userattrs_completion(const char* text, int state)
//{
//    static vector<string> validAttrs;
//    if (state == 0)
//    {
//        validAttrs.clear();
////        validAttrs = cmdexecuter->getUserAttrs(); //TODO: this should be asked via socket
//    }

//    if (validAttrs.size() == 0)
//    {
//        return empty_completion(text, state);
//    }

//    return generic_completion(text, state, validAttrs);
//}

//void discardOptionsAndFlags(vector<string> *ws)
//{
//    for (std::vector<string>::iterator it = ws->begin(); it != ws->end(); )
//    {
//        /* std::cout << *it; ... */
//        string w = ( string ) * it;
//        if (w.length() && ( w.at(0) == '-' )) //begins with "-"
//        {
//            it = ws->erase(it);
//        }
//        else //not an option/flag
//        {
//            ++it;
//        }
//    }
//}

//rl_compentry_func_t *getCompletionFunction(vector<string> words)
//{
//    // Strip words without flags
//    string thecommand = words[0];

//    if (words.size() > 1)
//    {
//        string lastword = words[words.size() - 1];
//        if (lastword.find_first_of("-") == 0)
//        {
//            if (lastword.find_last_of("=") != string::npos)
//            {
//                return flags_value_completion;
//            }
//            else
//            {
//                return flags_completion;
//            }
//        }
//    }
//    discardOptionsAndFlags(&words);

//    int currentparameter = words.size() - 1;
//    if (stringcontained(thecommand.c_str(), localremotefolderpatterncommands))
//    {
//        if (currentparameter == 1)
//        {
//            return local_completion;
//        }
//        if (currentparameter == 2)
//        {
//            return remotefolders_completion;
//        }
//    }
//    else if (thecommand == "put")
//    {
//        if (currentparameter == 1)
//        {
//            return local_completion;
//        }
//        else
//        {
//            return remotepaths_completion;
//        }
//    }
//    else if (stringcontained(thecommand.c_str(), remotepatterncommands))
//    {
//        if (currentparameter == 1)
//        {
//            return remotepaths_completion;
//        }
//    }
//    else if (stringcontained(thecommand.c_str(), remotefolderspatterncommands))
//    {
//        if (currentparameter == 1)
//        {
//            return remotefolders_completion;
//        }
//    }
//    else if (stringcontained(thecommand.c_str(), multipleremotepatterncommands))
//    {
//        if (currentparameter >= 1)
//        {
//            return remotepaths_completion;
//        }
//    }
//    else if (stringcontained(thecommand.c_str(), localpatterncommands))
//    {
//        if (currentparameter == 1)
//        {
//            return local_completion;
//        }
//    }
//    else if (stringcontained(thecommand.c_str(), remoteremotepatterncommands))
//    {
//        if (( currentparameter == 1 ) || ( currentparameter == 2 ))
//        {
//            return remotepaths_completion;
//        }
//    }
//    else if (stringcontained(thecommand.c_str(), remotelocalpatterncommands))
//    {
//        if (currentparameter == 1)
//        {
//            return remotepaths_completion;
//        }
//        if (currentparameter == 2)
//        {
//            return local_completion;
//        }
//    }
//    else if (stringcontained(thecommand.c_str(), emailpatterncommands))
//    {
//        if (currentparameter == 1)
//        {
//            return contacts_completion;
//        }
//    }
//    else if (thecommand == "import")
//    {
//        if (currentparameter == 2)
//        {
//            return remotepaths_completion;
//        }
//    }
//    else if (thecommand == "killsession")
//    {
//        if (currentparameter == 1)
//        {
//            return sessions_completion;
//        }
//    }
//    else if (thecommand == "attr")
//    {
//        if (currentparameter == 1)
//        {
//            return remotepaths_completion;
//        }
//        if (currentparameter == 2)
//        {
//            return nodeattrs_completion;
//        }
//    }
//    else if (thecommand == "userattr")
//    {
//        if (currentparameter == 1)
//        {
//            return userattrs_completion;
//        }
//    }
//    else if (thecommand == "log")
//    {
//        if (currentparameter == 1)
//        {
//            return loglevels_completion;
//        }
//    }
//    return empty_completion;
//}


char* generic_completion(const char* text, int state, vector<string> validOptions)
{
    static size_t list_index, len;
    static bool foundone;
    string name;
    if (!validOptions.size()) // no matches
    {
        return empty_completion(text,state); //dont fall back to filenames
    }
    if (!state)
    {
        list_index = 0;
        foundone = false;
        len = strlen(text);
    }
    while (list_index < validOptions.size())
    {
        name = validOptions.at(list_index);
        if (!rl_completion_quote_character) {
            escapeEspace(name);
        }

        list_index++;

        if (!( strcmp(text, "")) || (( name.size() >= len ) && ( strlen(text) >= len ) && ( name.find(text) == 0 )))
        {
            if (name.size() && (( name.at(name.size() - 1) == '=' ) || ( name.at(name.size() - 1) == '/' )))
            {
                rl_completion_suppress_append = 1;
            }
            foundone = true;
            return dupstr((char*)name.c_str());
        }
    }

    if (!foundone)
    {
        return empty_completion(text,state); //dont fall back to filenames
    }

    return((char*)NULL );
}

inline bool ends_with(std::string const & value, std::string const & ending)
{
    if (ending.size() > value.size()) return false;
    return std::equal(ending.rbegin(), ending.rend(), value.rbegin());
}

char* local_completion(const char* text, int state)
{
    return((char*)NULL );  //matches will be NULL: readline will use local completion
}

char* remote_completion(const char* text, int state)
{
    char *saved_line = strdup(getCurrentThreadLine().c_str());

    static vector<string> validOptions;
    if (state == 0)
    {
        validOptions.clear();
        string completioncommand("completionshell ");
        completioncommand+=saved_line;

        string s;
        ostringstream oss(s);

        comms->executeCommand(completioncommand,oss);

        string outputcommand = oss.str();
        if (outputcommand == "MEGACMD_USE_LOCAL_COMPLETION")
        {
            return local_completion(text,state); //fallback to local path completion
        }

        char *ptr = (char *)outputcommand.c_str();
        char *beginopt = ptr;
        while (*ptr)
        {
            if (*ptr == 0x1F)
            {
                *ptr = '\0';
                validOptions.push_back(beginopt);

                beginopt=ptr+1;
            }
            ptr++;
        }
        if (*beginopt)
        {
            validOptions.push_back(beginopt);
        }
    }

    free(saved_line);

    return generic_completion(text, state, validOptions);
}

static char** getCompletionMatches(const char * text, int start, int end)
{
    rl_filename_quoting_desired = 1;

    char **matches;

    matches = (char**)NULL;

    if (start == 0)
    {
        matches = rl_completion_matches((char*)text, &remote_completion);

        if (matches == NULL)
        {
            matches = rl_completion_matches((char*)text, &empty_completion);
        }
    }
    else
    {
        matches = rl_completion_matches((char*)text, remote_completion);

    }
    return( matches );
}

void printHistory()
{
    int length = history_length;
    int offset = 1;
    int rest = length;
    while (rest >= 10)
    {
        offset++;
        rest = rest / 10;
    }

//    mutexHistory.lock(); //TODO: use mutex
    for (int i = 0; i < length; i++)
    {
        history_set_pos(i);
        OUTSTREAM << setw(offset) << i << "  " << current_history()->line << endl;
    }
//    mutexHistory.unlock();
}


const char * getUsageStr(const char *command)
{
    if (!strcmp(command, "login"))
    {
        return "login [email [password]] | exportedfolderurl#key | session";
    }
    if (!strcmp(command, "begin"))
    {
        return "begin [ephemeralhandle#ephemeralpw]";
    }
    if (!strcmp(command, "signup"))
    {
        return "signup email [password] [--name=\"Your Name\"]";
    }
    if (!strcmp(command, "confirm"))
    {
        return "confirm link email [password]";
    }
    if (!strcmp(command, "session"))
    {
        return "session";
    }
    if (!strcmp(command, "mount"))
    {
        return "mount";
    }
    if (!strcmp(command, "unicode"))
    {
        return "unicode";
    }
    if (!strcmp(command, "ls"))
    {
        return "ls [-lRr] [remotepath]";
    }
    if (!strcmp(command, "cd"))
    {
        return "cd [remotepath]";
    }
    if (!strcmp(command, "log"))
    {
        return "log [-sc] level";
    }
    if (!strcmp(command, "du"))
    {
        return "du [remotepath remotepath2 remotepath3 ... ]";
    }
    if (!strcmp(command, "pwd"))
    {
        return "pwd";
    }
    if (!strcmp(command, "lcd"))
    {
        return "lcd [localpath]";
    }
    if (!strcmp(command, "lpwd"))
    {
        return "lpwd";
    }
    if (!strcmp(command, "import"))
    {
        return "import exportedfilelink#key [remotepath]";
    }
    if (!strcmp(command, "put"))
    {
        return "put  [-c] [-q] [--ignore-quota-warn] localfile [localfile2 localfile3 ...] [dstremotepath]";
    }
    if (!strcmp(command, "putq"))
    {
        return "putq [cancelslot]";
    }
    if (!strcmp(command, "get"))
    {
        return "get [-m] [-q] [--ignore-quota-warn] exportedlink#key|remotepath [localpath]";
    }
    if (!strcmp(command, "getq"))
    {
        return "getq [cancelslot]";
    }
    if (!strcmp(command, "pause"))
    {
        return "pause [get|put] [hard] [status]";
    }
    if (!strcmp(command, "attr"))
    {
        return "attr remotepath [-s attribute value|-d attribute]";
    }
    if (!strcmp(command, "userattr"))
    {
        return "userattr [-s attribute value|attribute] [--user=user@email]";
    }
    if (!strcmp(command, "mkdir"))
    {
        return "mkdir [-p] remotepath";
    }
    if (!strcmp(command, "rm"))
    {
        return "rm [-r] [-f] remotepath";
    }
    if (!strcmp(command, "mv"))
    {
        return "mv srcremotepath dstremotepath";
    }
    if (!strcmp(command, "cp"))
    {
        return "cp srcremotepath dstremotepath|dstemail:";
    }
    if (!strcmp(command, "sync"))
    {
        return "sync [localpath dstremotepath| [-ds] [ID|localpath]";
    }
    if (!strcmp(command, "https"))
    {
        return "https [on|off]";
    }
    if (!strcmp(command, "export"))
    {
        return "export [-d|-a [--expire=TIMEDELAY]] [remotepath]";
    }
    if (!strcmp(command, "share"))
    {
        return "share [-p] [-d|-a --with=user@email.com [--level=LEVEL]] [remotepath]";
    }
    if (!strcmp(command, "invite"))
    {
        return "invite [-d|-r] dstemail [--message=\"MESSAGE\"]";
    }
    if (!strcmp(command, "ipc"))
    {
        return "ipc email|handle -a|-d|-i";
    }
    if (!strcmp(command, "showpcr"))
    {
        return "showpcr";
    }
    if (!strcmp(command, "users"))
    {
        return "users [-s] [-h] [-n] [-d contact@email]";
    }
    if (!strcmp(command, "getua"))
    {
        return "getua attrname [email]";
    }
    if (!strcmp(command, "putua"))
    {
        return "putua attrname [del|set string|load file]";
    }
    if (!strcmp(command, "speedlimit"))
    {
        return "speedlimit [-u|-d] [-h] [NEWLIMIT]";
    }
    if (!strcmp(command, "killsession"))
    {
        return "killsession [-a|sessionid]";
    }
    if (!strcmp(command, "whoami"))
    {
        return "whoami [-l]";
    }
    if (!strcmp(command, "passwd"))
    {
        return "passwd [oldpassword newpassword]";
    }
    if (!strcmp(command, "retry"))
    {
        return "retry";
    }
    if (!strcmp(command, "recon"))
    {
        return "recon";
    }
    if (!strcmp(command, "reload"))
    {
        return "reload";
    }
    if (!strcmp(command, "logout"))
    {
        return "logout [--keep-session]";
    }
    if (!strcmp(command, "symlink"))
    {
        return "symlink";
    }
    if (!strcmp(command, "version"))
    {
        return "version [-l][-c]";
    }
    if (!strcmp(command, "debug"))
    {
        return "debug";
    }
    if (!strcmp(command, "chatf"))
    {
        return "chatf ";
    }
    if (!strcmp(command, "chatc"))
    {
        return "chatc group [email ro|rw|full|op]*";
    }
    if (!strcmp(command, "chati"))
    {
        return "chati chatid email ro|rw|full|op";
    }
    if (!strcmp(command, "chatr"))
    {
        return "chatr chatid [email]";
    }
    if (!strcmp(command, "chatu"))
    {
        return "chatu chatid";
    }
    if (!strcmp(command, "chatga"))
    {
        return "chatga chatid nodehandle uid";
    }
    if (!strcmp(command, "chatra"))
    {
        return "chatra chatid nodehandle uid";
    }
    if (!strcmp(command, "quit"))
    {
        return "quit";
    }
    if (!strcmp(command, "history"))
    {
        return "history";
    }
    if (!strcmp(command, "thumbnail"))
    {
        return "thumbnail [-s] remotepath localpath";
    }
    if (!strcmp(command, "preview"))
    {
        return "preview [-s] remotepath localpath";
    }
    if (!strcmp(command, "find"))
    {
        return "find [remotepath] [-l] [--pattern=PATTERN]";
    }
    if (!strcmp(command, "help"))
    {
        return "help [-f]";
    }
    if (!strcmp(command, "clear"))
    {
        return "clear";
    }
    if (!strcmp(command, "transfers"))
    {
        return "transfers [-c TAG|-a] | [-r TAG|-a]  | [-p TAG|-a] [--only-downloads | --only-uploads] [SHOWOPTIONS]";
    }
    return "command not found";
}

bool validCommand(string thecommand)
{
    return stringcontained((char*)thecommand.c_str(), validCommands);
}

string getsupportedregexps()
{
#ifdef USE_PCRE
        return "Perl Compatible Regular Expressions";
#elif __cplusplus >= 201103L
        return "c++11 Regular Expressions";
#else
        return "it accepts wildcards: ? and *. e.g.: ls f*00?.txt";
#endif
}

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>

/**
 * @brief getcharacterreadlineUTF16support
 * while this works, somehow arrows and other readline stuff is disabled using this one.
 * @param stream
 * @return
 */
int getcharacterreadlineUTF16support (FILE *stream)
{
    int result;
    char b[10];
    memset(b,0,10);

    while (1)
    {
        int oldmode = _setmode(fileno(stream), _O_U16TEXT);

        result = read (fileno (stream), &b, 10);
        _setmode(fileno(stream), oldmode);

        if (result == 0)
        {
            return (EOF);
        }

        // convert the UTF16 string to widechar
        size_t wbuffer_size;
        mbstowcs_s(&wbuffer_size, NULL, 0, b, _TRUNCATE);
        wchar_t *wbuffer = new wchar_t[wbuffer_size];
        mbstowcs_s(&wbuffer_size, wbuffer, wbuffer_size, b, _TRUNCATE);

        // convert the UTF16 widechar to UTF8 string
        string receivedutf8;
        MegaApi::utf16ToUtf8(wbuffer, wbuffer_size,&receivedutf8);

        if (strlen(receivedutf8.c_str()) > 1) //multi byte utf8 sequence: place the UTF8 characters into rl buffer one by one
        {
            for (u_int i=0;i< strlen(receivedutf8.c_str());i++)
            {
                rl_line_buffer[rl_end++] = receivedutf8.c_str()[i];
                rl_point=rl_end;
            }
            rl_line_buffer[rl_end] = '\0';

            return 0;
        }

        if (result =! 0)
        {
            return (b[0]);
        }

        /* If zero characters are returned, then the file that we are
     reading from is empty!  Return EOF in that case. */
        if (result == 0)
        {
            return (EOF);
        }
    }
}
#endif

void wait_for_input(int readline_fd)
{
    //TODO: test in windows
    fd_set fds;

    FD_ZERO(&fds);
    FD_SET(readline_fd, &fds);

    int rc = select(FD_SETSIZE, &fds, NULL, NULL, NULL);
    if (rc < 0)
    {
        if (errno != EINTR)  //syscall
        {
#ifdef _WIN32
            if (errno != ENOENT) // unexpectedly enters here, although works fine TODO: review this
#endif
                cerr << "Error at select: " << errno << endl;
            return;
        }
    }
}

// main loop
void megacmd()
{
    char *saved_line = NULL;
    int saved_point = 0;

    rl_save_prompt();

    int readline_fd = -1;
    if (!consoleFailed)
    {
        readline_fd = fileno(rl_instream);
    }

    static bool firstloop = true;

    comms->registerForStateChanges();
    usleep(1000); //give it a while to communicate the state

    for (;; )
    {
        if (prompt == COMMAND)
        {
            if (!handlerinstalled || !firstloop)
            {
                if (!consoleFailed)
                {
                    rl_callback_handler_install(*dynamicprompt ? dynamicprompt : prompts[COMMAND], store_line);
                }
                handlerinstalled = false;

                // display prompt
                if (saved_line)
                {
                    rl_replace_line(saved_line, 0);
                    free(saved_line);
                }

                rl_point = saved_point;
                rl_redisplay();
            }
        }

        firstloop = false;


        // command editing loop - exits when a line is submitted
        for (;; )
        {

            if (prompt == COMMAND || prompt == AREYOUSURETODELETE)
            {
                wait_for_input(readline_fd);

                //api->retryPendingConnections(); //TODO: this should go to the server!

                rl_callback_read_char();
                if (doExit)
                {
                    return;
                }
            }
            else
            {
                //console->readpwchar(pw_buf, sizeof pw_buf, &pw_buf_pos, &line);//TODO: use console
            }

            if (line)
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
            if (strlen(line))
            {
                // execute user command
                comms->executeCommand(line);
                if (!strcmp(line,"exit") || !strcmp(line,"quit"))
                {
                    doExit = true;
                }
            }
            free(line);
            line = NULL;
        }
        if (doExit)
        {
            if (saved_line != NULL)
                free(saved_line);
            return;
        }
    }
}

class NullBuffer : public std::streambuf
{
public:
    int overflow(int c)
    {
        return c;
    }
};

void printCenteredLine(string msj, u_int width, bool encapsulated = true)
{
    if (msj.size()>width)
    {
        width = msj.size();
    }
    if (encapsulated)
        COUT << "|";
    for (u_int i = 0; i < (width-msj.size())/2; i++)
        COUT << " ";
    COUT << msj;
    for (u_int i = 0; i < (width-msj.size())/2 + (width-msj.size())%2 ; i++)
        COUT << " ";
    if (encapsulated)
        COUT << "|";
    COUT << endl;
}

void printWelcomeMsg()
{
    u_int width = 75;
    int rows = 1, cols = width;
#if defined( RL_ISSTATE ) && defined( RL_STATE_INITIALIZED )

    if (RL_ISSTATE(RL_STATE_INITIALIZED))
    {
        rl_resize_terminal();
        rl_get_screen_size(&rows, &cols);
    }
#endif

    if (cols)
    {
        width = cols-2;
#ifdef _WIN32
        width--;
#endif
    }

    COUT << endl;
    COUT << ".";
    for (u_int i = 0; i < width; i++)
        COUT << "=" ;
    COUT << ".";
    COUT << endl;
    printCenteredLine(" __  __                   ____ __  __ ____  ",width);
    printCenteredLine("|  \\/  | ___  __ _  __ _ / ___|  \\/  |  _ \\ ",width);
    printCenteredLine("| |\\/| |/ _ \\/ _` |/ _` | |   | |\\/| | | | |",width);
    printCenteredLine("| |  | |  __/ (_| | (_| | |___| |  | | |_| |",width);
    printCenteredLine("|_|  |_|\\___|\\__, |\\__,_|\\____|_|  |_|____/ ",width);
    printCenteredLine("             |___/                          ",width);
    COUT << "|";
    for (u_int i = 0; i < width; i++)
        COUT << " " ;
    COUT << "|";
    COUT << endl;
    printCenteredLine("Welcome to MegaCMD! A Command Line Interactive and Scriptable",width);
    printCenteredLine("Application to interact with your MEGA account",width);
    printCenteredLine("This is a BETA version, it might not be bug-free.",width);
    printCenteredLine("Also, the signature/output of the commands may change in a future.",width);
    printCenteredLine("Please write to support@mega.nz if you find any issue or",width);
    printCenteredLine("have any suggestion concerning its functionalities.",width);
    printCenteredLine("Enter \"help --non-interactive\" to learn how to use MEGAcmd with scripts.",width);
    printCenteredLine("Enter \"help\" for basic info and a list of available commands.",width);

    COUT << "`";
    for (u_int i = 0; i < width; i++)
        COUT << "=" ;
    COUT << "´";
    COUT << endl;

}

int quote_detector(char *line, int index)
{
    return (
        index > 0 &&
        line[index - 1] == '\\' &&
        !quote_detector(line, index - 1)
    );
}


bool runningInBackground()
{
#ifndef _WIN32
    pid_t fg = tcgetpgrp(STDIN_FILENO);
    if(fg == -1) {
        // Piped:
        return false;
    }  else if (fg == getpgrp()) {
        // foreground
        return false;
    } else {
        // background
        return true;
    }
#endif
    return false;
}

#ifdef _WIN32
void mycompletefunct(char **c, int num_matches, int max_length)
{
    int rows = 1, cols = 80;

#if defined( RL_ISSTATE ) && defined( RL_STATE_INITIALIZED )

            if (RL_ISSTATE(RL_STATE_INITIALIZED))
            {
                rl_resize_terminal();
                rl_get_screen_size(&rows, &cols);
            }
#endif

    OUTSTREAM << endl;
    int nelements_per_col = (cols-1)/(max_length+1);
    for (int i=1; i < num_matches; i++)
    {
        OUTSTREAM << setw(max_length+1) << left << c[i];
        if ( (i%nelements_per_col == 0) && (i != num_matches-1))
        {
            OUTSTREAM << endl;
        }
    }
    OUTSTREAM << endl;
}
#endif

int main(int argc, char* argv[])
{
#ifdef _WIN32
    // Set Environment's default locale
    setlocale(LC_ALL, "");
    rl_completion_display_matches_hook = mycompletefunct;
#endif

#ifdef __MACH__
    initializeMacOSStuff(argc,argv);
#endif

//    mutexHistory.init(false);

    // intialize the comms object
    comms = new MegaCmdShellCommunications();

    // set up the console
#ifdef _WIN32
    console = new CONSOLE_CLASS;
#else
    struct termios term;
    if ( ( tcgetattr(STDIN_FILENO, &term) < 0 ) || runningInBackground() ) //try console
    {
        consoleFailed = true;
//        console = NULL;//TODO: use console
    }
    else
    {
//        console = new CONSOLE_CLASS; //TODO: use console
    }
#endif
//    cm = new COMUNICATIONMANAGER();

#if _WIN32
    if( SetConsoleCtrlHandler( (PHANDLER_ROUTINE) CtrlHandler, TRUE ) )
     {
        LOG_debug << "Control handler set";
     }
     else
     {
        LOG_warn << "Control handler set";
     }
#else
    // prevent CTRL+C exit
    if (!consoleFailed){
        signal(SIGINT, sigint_handler);
    }
#endif

//    atexit(finalize);//TODO: reset?

    //TODO: local completion is failing
    rl_attempted_completion_function = getCompletionMatches;
    rl_completer_quote_characters = "\"'";
    rl_filename_quote_characters  = " ";
    rl_completer_word_break_characters = (char *)" ";

    rl_char_is_quoted_p = &quote_detector;


    if (!runningInBackground())
    {
        rl_initialize(); // initializes readline,
        // so that we can use rl_message or rl_resize_terminal safely before ever
        // prompting anything.
    }

    printWelcomeMsg();
    if (consoleFailed)
    {
        cerr << "Couldn't initialize interactive CONSOLE. Running as non-interactive ONLY" << endl;
    }

    megacmd();
//    finalize(); //TODO: reset?
}
