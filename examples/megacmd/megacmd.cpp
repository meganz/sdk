/**
 * @file examples/megacmd/megacmd.cpp
 * @brief MegaCMD: Interactive CLI and service application
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

#include "megacmd.h"
//#include "mega.h"

#include "megacmdexecuter.h"
#include "megacmdutils.h"
#include "configurationmanager.h"
#include "megacmdlogger.h"
#include "comunicationsmanager.h"
#include "listeners.h"

#define USE_VARARGS
#define PREFER_STDARG

#include <readline/readline.h>
#include <readline/history.h>
#include <iomanip>
#include <string>


#ifdef _WIN32
#define COMUNICATIONMANAGER ComunicationsManager
#else
#include "comunicationsmanagerfilesockets.h"
#define COMUNICATIONMANAGER ComunicationsManagerFileSockets
#include <signal.h>
#endif

using namespace mega;

MegaCmdExecuter *cmdexecuter;

MegaSemaphore semaphoreClients; //to limit max parallel petitions

MegaApi *api;

//api objects for folderlinks
std::queue<MegaApi *> apiFolders;
std::vector<MegaApi *> occupiedapiFolders;
MegaSemaphore semaphoreapiFolders;
MegaMutex mutexapiFolders;

MegaCMDLogger *loggerCMD;

std::vector<MegaThread *> petitionThreads;

//Comunications Manager
ComunicationsManager * cm;

// global listener
MegaCmdGlobalListener* megaCmdGlobalListener;

MegaCmdMegaListener* megaCmdMegaListener;

bool loginInAtStartup = false;

string validGlobalParameters[] = {"v", "help"};

string alocalremotepatterncommands [] = {"put", "sync"};
vector<string> localremotepatterncommands(alocalremotepatterncommands, alocalremotepatterncommands + sizeof alocalremotepatterncommands / sizeof alocalremotepatterncommands[0]);

string aremotepatterncommands[] = {"export", "find"};
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


//string avalidCommands [] = { "login", "begin", "signup", "confirm", "session", "mount", "ls", "cd", "log", "pwd", "lcd", "lpwd",
//"import", "put", "put", "putq", "get", "get", "get", "getq", "pause", "getfa", "mkdir", "rm", "mv",
//"cp", "sync", "export", "export", "share", "share", "invite", "ipc", "showpcr", "users", "getua",
//"putua", "putbps", "killsession", "whoami", "passwd", "retry", "recon", "reload", "logout", "locallogout",
//"symlink", "version", "debug", "chatf", "chatc", "chati", "chatr", "chatu", "chatga", "chatra", "quit",
//"history" };
string avalidCommands [] = { "login", "signup", "confirm", "session", "mount", "ls", "cd", "log", "debug", "pwd", "lcd", "lpwd", "import",
                             "put", "get", "attr", "userattr", "mkdir", "rm", "du", "mv", "cp", "sync", "export", "share", "invite", "ipc", "showpcr", "users",
                             "putbps", "killsession", "whoami", "help",
                             "passwd", "reload", "logout", "version", "quit", "history", "thumbnail", "preview", "find", "completion" };
vector<string> validCommands(avalidCommands, avalidCommands + sizeof avalidCommands / sizeof avalidCommands[0]);


// password change-related state information
string oldpasswd;
string newpasswd;

bool doExit = false;
bool consoleFailed = false;

static char dynamicprompt[128];

static char* line;

static prompttype prompt = COMMAND;

static char pw_buf[256];
static int pw_buf_pos;

// local console
Console* console;

MegaMutex mutexHistory;

map<int, string> threadline;

string getCurrentThreadLine()
{
    uint64_t currentThread = MegaThread::currentThreadId();
    if (threadline.find(currentThread) == threadline.end())
    {
        char *saved_line = rl_copy_text(0, rl_point);
        string toret(saved_line);
        free(saved_line);
        return toret;
    }
    else
    {
        return threadline[currentThread];
    }
}

void setCurrentThreadLine(string s)
{
    threadline[MegaThread::currentThreadId()] = s;
}

void setCurrentThreadLine(const vector<string>& vec)
{
   setCurrentThreadLine(joinStrings(vec));
}

void sigint_handler(int signum)
{
    LOG_verbose << "Received signal: " << signum;
    if (loginInAtStartup)
    {
        exit(-2);
    }

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


void setprompt(prompttype p)
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

void changeprompt(const char *newprompt)
{
    strncpy(dynamicprompt, newprompt, sizeof( dynamicprompt ));
}

// readline callback - exit if EOF, add to history unless password
static void store_line(char* l)
{
    if (!l)
    {
        doExit = true;
        rl_set_prompt("(CTRL+D) Exiting ...\n");
        return;
    }

    if (*l && ( prompt == COMMAND ))
    {
        mutexHistory.lock();
        add_history(l);
        mutexHistory.unlock();
    }

    line = l;
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
    }
    else if ("rm" == thecommand)
    {
        validParams->insert("r");
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
        validParams->insert("delete-session");
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
    }
}

char* generic_completion(const char* text, int state, vector<string> validOptions)
{
    static size_t list_index, len;
    string name;

    if (!state)
    {
        list_index = 0;
        len = strlen(text);
    }
    while (list_index < validOptions.size())
    {
        name = validOptions.at(list_index);
        list_index++;

        if (!( strcmp(text, "")) || (( name.size() >= len ) && ( strlen(text) >= len ) && ( name.find(text) == 0 )))
        {
            if (name.size() && (( name.at(name.size() - 1) == '=' ) || ( name.at(name.size() - 1) == '/' )))
            {
                rl_completion_suppress_append = 1;
            }

            return dupstr((char*)name.c_str());
        }
    }

    return((char*)NULL );
}

char* commands_completion(const char* text, int state)
{
    return generic_completion(text, state, validCommands);
}

char* local_completion(const char* text, int state)
{
    return((char*)NULL );  //matches will be NULL: readline will use local completion
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

void addGlobalFlags(set<string> *setvalidparams)
{
    for (size_t i = 0; i < sizeof( validGlobalParameters ) / sizeof( *validGlobalParameters ); i++)
    {
        setvalidparams->insert(validGlobalParameters[i]);
    }
}

char * flags_completion(const char*text, int state)
{
    static vector<string> validparams;
    if (state == 0)
    {
        validparams.clear();
        char *saved_line = strdup(getCurrentThreadLine().c_str());
        vector<string> words = getlistOfWords(saved_line);
        free(saved_line);
        if (words.size())
        {
            set<string> setvalidparams;
            set<string> setvalidOptValues;
            addGlobalFlags(&setvalidparams);

            string thecommand = words[0];
            insertValidParamsPerCommand(&setvalidparams, thecommand, &setvalidOptValues);
            set<string>::iterator it;
            for (it = setvalidparams.begin(); it != setvalidparams.end(); it++)
            {
                string param = *it;
                string toinsert;

                if (param.size() > 1)
                {
                    toinsert = "--" + param;
                }
                else
                {
                    toinsert = "-" + param;
                }

                validparams.push_back(toinsert);
            }

            for (it = setvalidOptValues.begin(); it != setvalidOptValues.end(); it++)
            {
                string param = *it;
                string toinsert;

                if (param.size() > 1)
                {
                    toinsert = "--" + param + '=';
                }
                else
                {
                    toinsert = "-" + param + '=';
                }

                validparams.push_back(toinsert);
            }
        }
    }
    char *toret = generic_completion(text, state, validparams);
    return toret;
}

char * flags_value_completion(const char*text, int state)
{
    static vector<string> validValues;

    if (state == 0)
    {
        validValues.clear();

        char *saved_line = strdup(getCurrentThreadLine().c_str());
        vector<string> words = getlistOfWords(saved_line);
        free(saved_line);
        if (words.size() > 1)
        {
            string thecommand = words[0];
            string currentFlag = words[words.size() - 1];

            map<string, string> cloptions;
            map<string, int> clflags;

            set<string> validParams;

            insertValidParamsPerCommand(&validParams, thecommand);

            if (setOptionsAndFlags(&cloptions, &clflags, &words, validParams, true))
            {
                // return invalid??
            }

            if (thecommand == "share")
            {
                if (currentFlag.find("--level=") == 0)
                {
                    validValues.push_back(getShareLevelStr(MegaShare::ACCESS_UNKNOWN));
                    validValues.push_back(getShareLevelStr(MegaShare::ACCESS_READ));
                    validValues.push_back(getShareLevelStr(MegaShare::ACCESS_READWRITE));
                    validValues.push_back(getShareLevelStr(MegaShare::ACCESS_FULL));
                    validValues.push_back(getShareLevelStr(MegaShare::ACCESS_OWNER));
                    validValues.push_back(getShareLevelStr(MegaShare::ACCESS_UNKNOWN));
                }
                if (currentFlag.find("--with=") == 0)
                {
                    validValues = cmdexecuter->getlistusers();
                }
            }
            if (( thecommand == "userattr" ) && ( currentFlag.find("--user=") == 0 ))
            {
                validValues = cmdexecuter->getlistusers();
            }
        }
    }

    char *toret = generic_completion(text, state, validValues);
    return toret;
}

char* remotepaths_completion(const char* text, int state)
{
    static vector<string> validpaths;
    if (state == 0)
    {
        string wildtext(text);
#ifdef USE_PCRE
        wildtext += ".";
#elif __cplusplus >= 201103L
        wildtext += ".";
#endif
        wildtext += "*";

        validpaths = cmdexecuter->listpaths(wildtext);
    }
    return generic_completion(text, state, validpaths);
}

char* remotefolders_completion(const char* text, int state)
{
    static vector<string> validpaths;
    if (state == 0)
    {
        string wildtext(text);
#ifdef USE_PCRE
        wildtext += ".";
#elif __cplusplus >= 201103L
        wildtext += ".";
#endif
        wildtext += "*";

        validpaths = cmdexecuter->listpaths(wildtext, true);
    }
    return generic_completion(text, state, validpaths);
}

char* loglevels_completion(const char* text, int state)
{
    static vector<string> validloglevels;
    if (state == 0)
    {
        validloglevels.push_back(getLogLevelStr(MegaApi::LOG_LEVEL_FATAL));
        validloglevels.push_back(getLogLevelStr(MegaApi::LOG_LEVEL_ERROR));
        validloglevels.push_back(getLogLevelStr(MegaApi::LOG_LEVEL_WARNING));
        validloglevels.push_back(getLogLevelStr(MegaApi::LOG_LEVEL_INFO));
        validloglevels.push_back(getLogLevelStr(MegaApi::LOG_LEVEL_DEBUG));
        validloglevels.push_back(getLogLevelStr(MegaApi::LOG_LEVEL_MAX));
    }
    return generic_completion(text, state, validloglevels);
}

char* contacts_completion(const char* text, int state)
{
    static vector<string> validcontacts;
    if (state == 0)
    {
        validcontacts = cmdexecuter->getlistusers();
    }
    return generic_completion(text, state, validcontacts);
}

char* sessions_completion(const char* text, int state)
{
    static vector<string> validSessions;
    if (state == 0)
    {
        validSessions = cmdexecuter->getsessions();
    }

    if (validSessions.size() == 0)
    {
        return empty_completion(text, state);
    }

    return generic_completion(text, state, validSessions);
}

char* nodeattrs_completion(const char* text, int state)
{
    static vector<string> validAttrs;
    if (state == 0)
    {
        validAttrs.clear();
        char *saved_line = strdup(getCurrentThreadLine().c_str());
        vector<string> words = getlistOfWords(saved_line);
        free(saved_line);
        if (words.size() > 1)
        {
            validAttrs = cmdexecuter->getNodeAttrs(words[1]);
        }
    }

    if (validAttrs.size() == 0)
    {
        return empty_completion(text, state);
    }

    return generic_completion(text, state, validAttrs);
}

void discardOptionsAndFlags(vector<string> *ws)
{
    for (std::vector<string>::iterator it = ws->begin(); it != ws->end(); )
    {
        /* std::cout << *it; ... */
        string w = ( string ) * it;
        if (w.length() && ( w.at(0) == '-' )) //begins with "-"
        {
            it = ws->erase(it);
        }
        else //not an option/flag
        {
            ++it;
        }
    }
}

rl_compentry_func_t *getCompletionFunction(vector<string> words)
{
    // Strip words without flags
    string thecommand = words[0];

    if (words.size() > 1)
    {
        string lastword = words[words.size() - 1];
        if (lastword.find_first_of("-") == 0)
        {
            if (lastword.find_last_of("=") != string::npos)
            {
                return flags_value_completion;
            }
            else
            {
                return flags_completion;
            }
        }
    }
    discardOptionsAndFlags(&words);

    int currentparameter = words.size() - 1;
    if (stringcontained(thecommand.c_str(), localremotepatterncommands))
    {
        if (currentparameter == 1)
        {
            return local_completion;
        }
        if (currentparameter == 2)
        {
            return remotepaths_completion;
        }
    }
    else if (stringcontained(thecommand.c_str(), remotepatterncommands))
    {
        if (currentparameter == 1)
        {
            return remotepaths_completion;
        }
    }
    else if (stringcontained(thecommand.c_str(), remotefolderspatterncommands))
    {
        if (currentparameter == 1)
        {
            return remotefolders_completion;
        }
    }
    else if (stringcontained(thecommand.c_str(), multipleremotepatterncommands))
    {
        if (currentparameter >= 1)
        {
            return remotepaths_completion;
        }
    }
    else if (stringcontained(thecommand.c_str(), localpatterncommands))
    {
        if (currentparameter == 1)
        {
            return local_completion;
        }
    }
    else if (stringcontained(thecommand.c_str(), remoteremotepatterncommands))
    {
        if (( currentparameter == 1 ) || ( currentparameter == 2 ))
        {
            return remotepaths_completion;
        }
    }
    else if (stringcontained(thecommand.c_str(), remotelocalpatterncommands))
    {
        if (currentparameter == 1)
        {
            return remotepaths_completion;
        }
        if (currentparameter == 2)
        {
            return local_completion;
        }
    }
    else if (stringcontained(thecommand.c_str(), emailpatterncommands))
    {
        if (currentparameter == 1)
        {
            return contacts_completion;
        }
    }
    else if (thecommand == "import")
    {
        if (currentparameter == 2)
        {
            return remotepaths_completion;
        }
    }
    else if (thecommand == "killsession")
    {
        if (currentparameter == 1)
        {
            return sessions_completion;
        }
    }
    else if (thecommand == "attr")
    {
        if (currentparameter == 1)
        {
            return remotepaths_completion;
        }
        if (currentparameter == 2)
        {
            return nodeattrs_completion;
        }
    }
    else if (thecommand == "log")
    {
        if (currentparameter == 1)
        {
            return loglevels_completion;
        }
    }
    return empty_completion;
}

static char** getCompletionMatches(const char * text, int start, int end)
{
    char **matches;

    matches = (char**)NULL;

    if (start == 0)
    {
        matches = rl_completion_matches((char*)text, &commands_completion);
        if (matches == NULL)
        {
            matches = rl_completion_matches((char*)text, &empty_completion);
        }
    }
    else
    {
        char *saved_line = strdup(getCurrentThreadLine().c_str());
        vector<string> words = getlistOfWords(saved_line);
        if (strlen(saved_line) && ( saved_line[strlen(saved_line) - 1] == ' ' ))
        {
            words.push_back("");
        }
        free(saved_line);

        matches = rl_completion_matches((char*)text, getCompletionFunction(words));
    }
    return( matches );
}

string getListOfCompletionValues(vector<string> words)
{
    string completionValues;
    rl_compentry_func_t * compfunction = getCompletionFunction(words);
    int state=0;
    if (words.size()>1)
    while (true)
    {
        char *newval;
        string &lastword = words[words.size()-1];
        if (lastword.size()>3 && lastword[0]== '-' && lastword[1]== '-' && lastword.find('=')!=string::npos)
        {
            newval = compfunction(lastword.substr(lastword.find_first_of('=')+1).c_str(), state);
        }
        else
            newval = compfunction(lastword.c_str(), state);

        if (!newval) break;
        if (completionValues.size())
            completionValues+=" ";


        if (strstr(newval," "))
        {
            completionValues+="\"";
            completionValues+=newval;
            completionValues+="\"";
        }
        else
        {
            completionValues+=newval;
        }

        state++;
    }
    return completionValues;
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

    mutexHistory.lock();
    for (int i = 0; i < length; i++)
    {
        history_set_pos(i);
        OUTSTREAM << setw(offset) << i << "  " << current_history()->line << endl;
    }

    mutexHistory.unlock();
}

MegaApi* getFreeApiFolder()
{
    semaphoreapiFolders.wait();
    mutexapiFolders.lock();
    MegaApi* toret = apiFolders.front();
    apiFolders.pop();
    occupiedapiFolders.push_back(toret);
    mutexapiFolders.unlock();
    return toret;
}

void freeApiFolder(MegaApi *apiFolder)
{
    mutexapiFolders.lock();
    occupiedapiFolders.erase(std::remove(occupiedapiFolders.begin(), occupiedapiFolders.end(), apiFolder), occupiedapiFolders.end());
    apiFolders.push(apiFolder);
    semaphoreapiFolders.release();
    mutexapiFolders.unlock();
}

const char * getUsageStr(const char *command)
{
    if (!strcmp(command, "login"))
    {
        return "login [email [password] | exportedfolderurl#key | session";
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
        return "put localfile [localfile2 localfile3 ...] [-c] [dstremotepath]";
    }
    if (!strcmp(command, "putq"))
    {
        return "putq [cancelslot]";
    }
    if (!strcmp(command, "get"))
    {
        return "get exportedlink#key|remotepath [localpath]";
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
        return "rm [-r] remotepath";
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
        return "sync [localpath dstremotepath| [-ds] cancelslot]";
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
        return "users [-s] [-h] [-d contact@email]";
    }
    if (!strcmp(command, "getua"))
    {
        return "getua attrname [email]";
    }
    if (!strcmp(command, "putua"))
    {
        return "putua attrname [del|set string|load file]";
    }
    if (!strcmp(command, "putbps"))
    {
        return "putbps [limit|auto|none]";
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
        return "logout [--delete-session]";
    }
    if (!strcmp(command, "symlink"))
    {
        return "symlink";
    }
    if (!strcmp(command, "version"))
    {
        return "version";
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
        return "find [remotepath] [--pattern=PATTERN]";
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

string getHelpStr(const char *command)
{
    ostringstream os;

    os << getUsageStr(command) << endl;
    if (!strcmp(command, "login"))
    {
        os << "Logs into a mega" << endl;
        os << " You can log in either with email and password, with session ID, or into an exportedfolder" << endl;
        os << " If loging into an exported folder indicate url#key" << endl;
    }
    else if (!strcmp(command, "signup"))
    {
        os << "Register as user with a given email" << endl;
        os << endl;
        os << "Options:" << endl;
        os << " --name=\"Your Name\"" << "\t" << "Name to register. e.g. \"John Smith\"" << endl;
        os << endl;
        os << " You will receive an email to confirm your account. Once you have received the email, please proceed to confirm the link included in that email with \"confirm\"." << endl;
    }
    else if (!strcmp(command, "help"))
    {
        os << "Prints list of commands" << endl;
        os << endl;
        os << "Options:" << endl;
        os << " -f" << "\t" << "Indluce a brief description of the commands" << endl;
    }
    else if (!strcmp(command, "history"))
    {
        os << "Prints history of used commands" << endl;
    }
    else if (!strcmp(command, "confirm"))
    {
        os << "Confirm an account using the link provided after the \"singup\" process." << endl;
        os << " It requires the email and the password used to obtain the link." << endl;
        os << endl;
    }
    else if (!strcmp(command, "session"))
    {
        os << "Prints (secret) session ID" << endl;
    }
    else if (!strcmp(command, "mount"))
    {
        os << "Lists all the main nodes" << endl;
    }
    else if (!strcmp(command, "ls"))
    {
        os << "Lists files in a remote path" << endl;
        os << " remotepath can be a pattern (" << getsupportedregexps() << ") " << endl;
        os << " Also, constructions like /PATTERN1/PATTERN2/PATTERN3 are allowed" << endl;
        os << endl;
        os << "Options:" << endl;
        os << " -R|-r" << "\t" << "list folders recursively" << endl;
        os << " -l" << "\t" << "include extra information" << endl;
    }
    else if (!strcmp(command, "cd"))
    {
        os << "Changes the current remote folder" << endl;
        os << endl;
        os << "If no folder is provided, it will be changed to the root folder" << endl;
    }
    else if (!strcmp(command, "log"))
    {
        os << "Prints/Modifies the current logs level" << endl;
        os << endl;
        os << "Options:" << endl;
        os << " -c" << "\t" << "CMD log level (higher level messages). Messages captured by the command line." << endl;
        os << " -s" << "\t" << "SDK log level (lower level messages). Messages captured by the engine and libs" << endl;
    }
    else if (!strcmp(command, "du"))
    {
        os << "Prints size used by files/folders" << endl;
        os << " remotepath can be a pattern (" << getsupportedregexps() << ") " << endl;
    }
    else if (!strcmp(command, "pwd"))
    {
        os << "Prints the current remote folder" << endl;
    }
    else if (!strcmp(command, "lcd"))
    {
        os << "Changes the current local folder for the interactive console" << endl;
        os << endl;
        os << "It will be used for uploads and downloads" << endl;
        os << endl;
        os << "If not using interactive console, the current local folder will be that of the shell executing mega comands" << endl;
    }
    else if (!strcmp(command, "lpwd"))
    {
        os << "Prints the current local folder for the interactive console" << endl;
        os << endl;
        os << "It will be used for uploads and downloads" << endl;
        os << endl;
        os << "If not using interactive console, the current local folder will be that of the shell executing mega comands" << endl;
    }
    else if (!strcmp(command, "logout"))
    {
        os << "Logs out" << endl;
        os << endl;
        os << "Options:" << endl;
        os << " --delete-session" << "\t" << "Deletes the current session, cleaning all cached info." << endl;
    }
    else if (!strcmp(command, "import"))
    {
        os << "Imports the contents of a remote link into user's cloud" << endl;
        os << endl;
        os << "If no remote path is provided, the current local folder will be used" << endl;
    }
    else if (!strcmp(command, "put"))
    {
        os << "Uploads files/folders to a remote folder" << endl;
        os << endl;
        os << "Options:" << endl;
        os << " -c" << "\t" << "Creates remote folder destination in case of not existing." << endl;
    }
//    if(!strcmp(command,"putq") ) return "putq [cancelslot]";
    else if (!strcmp(command, "get"))
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
    if (!strcmp(command, "attr"))
    {
        os << "Lists/updates node attributes" << endl;
        os << endl;
        os << "Options:" << endl;
        os << " -s" << "\tattribute value \t" << "sets an attribute to a value" << endl;
        os << " -d" << "\tattribute       \t" << "removes the attribute" << endl;
    }
    if (!strcmp(command, "userattr"))
    {
        os << "Lists/updates user attributes" << endl;
        os << endl;
        os << "Options:" << endl;
        os << " -s" << "\tattribute value \t" << "sets an attribute to a value" << endl;
        os << " --user=user@email" << "\t" << "select the user to query/change" << endl;
    }
    else if (!strcmp(command, "mkdir"))
    {
        os << "Creates a directory or a directories hierarchy" << endl;
        os << endl;
        os << "Options:" << endl;
        os << " -p" << "\t" << "Allow recursive" << endl;
    }
    else if (!strcmp(command, "rm"))
    {
        os << "Deletes a remote file/folder" << endl;
        os << endl;
        os << "Options:" << endl;
        os << " -r" << "\t" << "Delete recursively (for folders)" << endl;
    }
    else if (!strcmp(command, "mv"))
    {
        os << "Moves a file/folder into a new location (all remotes)" << endl;
        os << endl;
        os << "If the location exists and is a folder, the source will be moved there" << endl;
        os << "If the location doesn't exits, the source will be renamed to the defined destiny" << endl;
    }
    else if (!strcmp(command, "cp"))
    {
        os << "Moves a file/folder into a new location (all remotes)" << endl;
        os << endl;
        os << "If the location exists and is a folder, the source will be copied there" << endl;
        os << "If the location doesn't exits, the file/folder will be renamed to the defined destiny" << endl;
    }
    else if (!strcmp(command, "sync"))
    {
        os << "Controls synchronizations" << endl;
        os << endl;
        os << "If no argument is provided, it lists current synchronization with their IDs and their state" << endl;
        os << endl;
        os << "If provided local and remote paths, it will start synchronizing a local folder into a remote folder" << endl;
        os << endl;
        os << "If an ID is provided, it will list such synchronization with its state, unless an option is specified:" << endl;
        os << "-d" << " " << "ID|localpath" << "\t" << "deletes a synchronization" << endl;
        os << "-s" << " " << "ID|localpath" << "\t" << "stops(pauses) a synchronization" << endl;
    }
    else if (!strcmp(command, "export"))
    {
        os << "Prints/Modifies the status of current exports" << endl;
        os << endl;
        os << "Options:" << endl;
        os << " -a" << "\t" << "Adds an export (or modifies it if existing)" << endl;
        os << " --expire=TIMEDELAY" << "\t" << "Determines the expiration time of a node." << endl;
        os << "                   " << "\t" << "   It indicates the delay in hours(h), days(d), minutes(M), seconds(s), months(m) or years(y)" << endl;
        os << "                   " << "\t" << "   e.g. \"1m12d3h\" stablish an expiration time 1 month, 12 days and 3 hours after the current moment" << endl;
        os << " -d" << "\t" << "Deletes an export" << endl;
        os << endl;
        os << "If a remote path is given it'll be used to add/delete or in case of no option selected," << endl;
        os << " it will display all the exports existing in the tree of that path" << endl;
    }
    else if (!strcmp(command, "share"))
    {
        os << "Prints/Modifies the status of current shares" << endl;
        os << endl;
        os << "Options:" << endl;
        os << " -p" << "\t" << "Show pending shares" << endl;
        os << " --with=email" << "\t" << "Determines the email of the user to [no longer] share with" << endl;
        os << " -d" << "\t" << "Stop sharing with the selected user" << endl;
        os << " -a" << "\t" << "Adds a share (or modifies it if existing)" << endl;
        os << " --level=LEVEL" << "\t" << "Level of acces given to the user" << endl;
        os << "              " << "\t" << "0: " << "Read access" << endl;
        os << "              " << "\t" << "1: " << "Read and write" << endl;
        os << "              " << "\t" << "2: " << "Full access" << endl;
        os << "              " << "\t" << "3: " << "Owner access" << endl;
        os << endl;
        os << "If a remote path is given it'll be used to add/delete or in case of no option selected," << endl;
        os << " it will display all the shares existing in the tree of that path" << endl;
    }
    else if (!strcmp(command, "invite"))
    {
        os << "Invites/deletes a contact" << endl;
        os << endl;
        os << "Options:" << endl;
        os << " -d" << "\t" << "Deletes contact" << endl;
        os << " -r" << "\t" << "Sends the invitation again" << endl;
        os << " --message=\"MESSAGE\"" << "\t" << "Sends inviting message" << endl;
        os << endl;
        os << "Use \"showpcr\" to browse invitations" << endl;
        os << "Use \"ipc\" to manage invitations received" << endl;
    }
    if (!strcmp(command, "ipc"))
    {
        os << "Manages contact invitations." << endl;
        os << endl;
        os << "Options:" << endl;
        os << " -a" << "\t" << "Accepts invitation" << endl;
        os << " -d" << "\t" << "Rejects invitation" << endl;
        os << " -i" << "\t" << "Ignores invitation [WARNING: do not use unless you know what you are doing]" << endl;
        os << endl;
        os << "Use \"invite\" to send invitations" << endl;
        os << "Use \"showpcr\" to browse invitations" << endl;
    }
    if (!strcmp(command, "showpcr"))
    {
        os << "Shows incoming and outcoming contact requests." << endl;
        os << endl;
        os << "Use \"ipc\" to manage invitations received" << endl;
    }
    else if (!strcmp(command, "users"))
    {
        os << "List contacts" << endl;
        os << endl;
        os << "Options:" << endl;
        os << " -s" << "\t" << "Show shared folders" << endl;
        os << " -h" << "\t" << "Show all contacts (hidden, blocked, ...)" << endl;
        os << " -d" << "\tcontact@email " << "Deletes the specified contact" << endl;
    }
//    if(!strcmp(command,"getua") ) return "getua attrname [email]";
//    if(!strcmp(command,"putua") ) return "putua attrname [del|set string|load file]";
    else if (!strcmp(command, "putbps"))
    {
        os << "Sets upload limit" << endl;
    }
    else if (!strcmp(command, "killsession"))
    {
        os << "Kills a session of current user." << endl;
        os << endl;
        os << "Options:" << endl;
        os << " -a" << "\t" << "kills all sessions except the current one" << endl;
        os << endl;
        os << "To see all session use \"whoami -l\"" << endl;
    }
    else if (!strcmp(command, "whoami"))
    {
        os << "Print info of the user" << endl;
        os << endl;
        os << "Options:" << endl;
        os << " -l" << "\t" << "Show extended info: total storage used, storage per main folder (see mount), pro level, account balance, and also the active sessions" << endl;
    }
    if (!strcmp(command, "passwd"))
    {
        os << "Modifies user password" << endl;
    }
//    if(!strcmp(command,"retry") ) return "retry";
//    if(!strcmp(command,"recon") ) return "recon";
    else if (!strcmp(command, "reload"))
    {
        os << "Forces a reload of the remote files of the user" << endl;
    }
//    if(!strcmp(command,"symlink") ) return "symlink";
    else if (!strcmp(command, "version"))
    {
        os << "Prints MEGA SDK version" << endl;
    }
    else if (!strcmp(command, "thumbnail"))
    {
        os << "To download/upload the thumbnail of a file. If no -s is inidicated, it will download the thumbnail" << endl;
        os << endl;
        os << "Options:" << endl;
        os << " -s" << "\t" << "Sets the thumbnail to the specified file" << endl;
    }
    else if (!strcmp(command, "preview"))
    {
        os << "To download/upload the preview of a file. If no -s is inidicated, it will download the preview" << endl;
        os << endl;
        os << "Options:" << endl;
        os << " -s" << "\t" << "Sets the preview to the specified file" << endl;
    }
    else if (!strcmp(command, "find"))
    {
        os << "Find nodes matching a pattern" << endl;
        os << endl;
        os << "Options:" << endl;
        os << " --pattern=PATTERN" << "\t" << "Pattern to match";
        os << " (" << getsupportedregexps() << ") " << endl;
    }
    if(!strcmp(command,"debug") )
    {
        os << "Enters debugging mode (HIGHLY VERBOSE)" << endl;
    }
//    if(!strcmp(command,"chatf") ) return "chatf ";
//    if(!strcmp(command,"chatc") ) return "chatc group [email ro|rw|full|op]*";
//    if(!strcmp(command,"chati") ) return "chati chatid email ro|rw|full|op";
//    if(!strcmp(command,"chatr") ) return "chatr chatid [email]";
//    if(!strcmp(command,"chatu") ) return "chatu chatid";
//    if(!strcmp(command,"chatga") ) return "chatga chatid nodehandle uid";
//    if(!strcmp(command,"chatra") ) return "chatra chatid nodehandle uid";
    else if (!strcmp(command, "quit"))
    {
        os << "Quits" << endl;
        os << endl;
        os << "Notice that the session will still be active, and local caches available" << endl;
        os << "The session will be resumed when the service is restarted" << endl;
    }

    return os.str();
}


void printAvailableCommands(int extensive = 0)
{
    vector<string> validCommandsOrdered = validCommands;
    sort(validCommandsOrdered.begin(), validCommandsOrdered.end());
    for (size_t i = 0; i < validCommandsOrdered.size(); i++)
    {
        if (validCommandsOrdered.at(i)!="completion")
        {
            OUTSTREAM << "      " << getUsageStr(validCommandsOrdered.at(i).c_str());
            if (extensive)
            {
                string helpstr = getHelpStr(validCommandsOrdered.at(i).c_str());
                helpstr=string(helpstr,helpstr.find_first_of("\n")+1);
                OUTSTREAM << ": " << string(helpstr,0,helpstr.find_first_of("\n"));
            }
            OUTSTREAM << endl;
        }
    }
}

void executecommand(char* ptr)
{
    vector<string> words = getlistOfWords(ptr);
    if (!words.size())
    {
        return;
    }

    string thecommand = words[0];

    if (( thecommand == "?" ) || ( thecommand == "h" ))
    {
        printAvailableCommands();
        return;
    }

    if (words[0] == "completion")
    {
        if (words.size() < 3) words.push_back("");
        vector<string> wordstocomplete(words.begin()+1,words.end());
        setCurrentThreadLine(wordstocomplete);
        OUTSTREAM << getListOfCompletionValues(wordstocomplete);
        return;
    }

    map<string, string> cloptions;
    map<string, int> clflags;

    set<string> validParams;
    addGlobalFlags(&validParams);

    if (setOptionsAndFlags(&cloptions, &clflags, &words, validParams, true))
    {
        setCurrentOutCode(MCMD_EARGS);
        LOG_err << "      " << getUsageStr(thecommand.c_str());
        return;
    }

    insertValidParamsPerCommand(&validParams, thecommand);

    if (!validCommand(thecommand))   //unknown command
    {
        setCurrentOutCode(MCMD_EARGS);
        LOG_err << "      " << getUsageStr("unknwon");
        return;
    }

    if (setOptionsAndFlags(&cloptions, &clflags, &words, validParams))
    {
        setCurrentOutCode(MCMD_EARGS);
        LOG_err << "      " << getUsageStr(thecommand.c_str());
        return;
    }
    setCurrentThreadLogLevel(MegaApi::LOG_LEVEL_ERROR + getFlag(&clflags, "v"));

    if (getFlag(&clflags, "help"))
    {
        string h = getHelpStr(thecommand.c_str());
        OUTSTREAM << h << endl;
        return;
    }


    if ( thecommand == "help" )
    {
        OUTSTREAM << "Here is the list of available commands and their usage" << endl;
        OUTSTREAM << "Use \"help\" -f to get a brief description of the commands" << endl;
        OUTSTREAM << "You can get further help on a specific command with \"command\" --help " << endl;
        OUTSTREAM << endl << "Commands:" << endl;

        printAvailableCommands(getFlag(&clflags,"f"));
        return;
    }

    cmdexecuter->executecommand(words, &clflags, &cloptions);
}


static void process_line(char* l)
{
    switch (prompt)
    {
        case LOGINPASSWORD:
        {
            if (!strlen(l))
            {
                break;
            }
            if (!cmdexecuter->confirming)
            {
                cmdexecuter->loginWithPassword(l);
            }
            else
            {
                cmdexecuter->confirmWithPassword(l);
                cmdexecuter->confirming = false;
            }
            setprompt(COMMAND);
            break;
        }

        case OLDPASSWORD:
        {
            if (!strlen(l))
            {
                break;
            }
            oldpasswd = l;
            OUTSTREAM << endl;
            setprompt(NEWPASSWORD);
            break;
        }

        case NEWPASSWORD:
        {
            if (!strlen(l))
            {
                break;
            }
            newpasswd = l;
            OUTSTREAM << endl;
            setprompt(PASSWORDCONFIRM);
        }
        break;

        case PASSWORDCONFIRM:
        {
            if (!strlen(l))
            {
                break;
            }
            if (l != newpasswd)
            {
                OUTSTREAM << endl << "New passwords differ, please try again" << endl;
            }
            else
            {
                OUTSTREAM << endl;
                if (!cmdexecuter->signingup)
                {
                    cmdexecuter->changePassword(oldpasswd.c_str(), newpasswd.c_str());
                }
                else
                {
                    cmdexecuter->signupWithPassword(l);
                    cmdexecuter->signingup = false;
                }
            }

            setprompt(COMMAND);
            break;
        }

        case COMMAND:
        {
            if (!l || !strcmp(l, "q") || !strcmp(l, "quit") || !strcmp(l, "exit"))
            {
                //                store_line(NULL);
                exit(0);
            }
            executecommand(l);
            break;
        }
    }
}

void * doProcessLine(void *pointer)
{
    CmdPetition *inf = (CmdPetition*)pointer;

    std::ostringstream s;
    setCurrentThreadOutStream(&s);
    setCurrentThreadLogLevel(MegaApi::LOG_LEVEL_ERROR);
    setCurrentOutCode(MCMD_OK);

    LOG_verbose << " Processing " << *inf << " in thread: " << MegaThread::currentThreadId() << " " << cm->get_petition_details(inf);

    process_line(inf->getLine());

    LOG_verbose << " Procesed " << *inf << " in thread: " << MegaThread::currentThreadId() << " " << cm->get_petition_details(inf);

    cm->returnAndClosePetition(inf, &s, getCurrentOutCode());

    semaphoreClients.release();
    return NULL;
}


void delete_finished_threads()
{
    for (std::vector<MegaThread *>::iterator it = petitionThreads.begin(); it != petitionThreads.end(); )
    {
        /* std::cout << *it; ... */
        MegaThread *mt = (MegaThread*)*it;
#ifdef USE_QT
        if (mt->isFinished())
        {
            delete mt;
            it = petitionThreads.erase(it);
        }
        else
#endif
        ++it;
    }
}

void finalize()
{
    LOG_info << "closing application ...";
    delete_finished_threads();
    delete cm;
    if (!consoleFailed)
    {
        delete console;
    }
    delete megaCmdMegaListener;
    delete api;
    while (!apiFolders.empty())
    {
        delete apiFolders.front();
        apiFolders.pop();
    }

    for (std::vector< MegaApi * >::iterator it = occupiedapiFolders.begin(); it != occupiedapiFolders.end(); ++it)
    {
        delete ( *it );
    }

    occupiedapiFolders.clear();

    delete megaCmdGlobalListener;
    delete cmdexecuter;

    LOG_debug << "resources have been cleaned ...";
    delete loggerCMD;
    ConfigurationManager::unloadConfiguration();
}

// main loop
void megacmd()
{
    char *saved_line = NULL;
    int saved_point = 0;

    rl_save_prompt();

    int readline_fd = fileno(rl_instream);

    for (;; )
    {
        if (prompt == COMMAND)
        {
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

        // command editing loop - exits when a line is submitted
        for (;; )
        {
            if (Waiter::HAVESTDIN)
            {
                if (prompt == COMMAND)
                {
                    if (consoleFailed)
                    {
                        cm->waitForPetition();
                    }
                    else
                    {
                        cm->waitForPetitionOrReadlineInput(readline_fd);
                    }
                    api->retryPendingConnections();

                    if (cm->receivedReadlineInput(readline_fd))
                    {
                        rl_callback_read_char();
                        if (doExit)
                        {
                            exit(0);
                        }
                    }
                    else if (cm->receivedPetition())
                    {
                        semaphoreClients.wait();
                        LOG_verbose << "Client connected ";

                        CmdPetition *inf = cm->getPetition();

                        LOG_verbose << "petition registered: " << *inf;

                        delete_finished_threads();

                        //append new one
                        MegaThread * petitionThread = new MegaThread();
                        petitionThreads.push_back(petitionThread);

                        LOG_debug << "starting processing: " << *inf;

                        petitionThread->start(doProcessLine, (void*)inf);
                    }
                }
                else
                {
                    console->readpwchar(pw_buf, sizeof pw_buf, &pw_buf_pos, &line);
                }
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
            // execute user command
            process_line(line);
            free(line);
            line = NULL;
        }
        if (doExit)
        {
            exit(0);
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

int main(int argc, char* argv[])
{
    NullBuffer null_buffer;
    std::ostream null_stream(&null_buffer);
    SimpleLogger::setAllOutputs(&null_stream);
    SimpleLogger::setLogLevel(logMax); // do not filter anything here, log level checking is done by loggerCMD


    loggerCMD = new MegaCMDLogger(&cout);

    loggerCMD->setApiLoggerLevel(MegaApi::LOG_LEVEL_ERROR);
    loggerCMD->setCmdLoggerLevel(MegaApi::LOG_LEVEL_DEBUG);

    if (( argc > 1 ) && !( strcmp(argv[1], "--debug")))
    {
        loggerCMD->setApiLoggerLevel(MegaApi::LOG_LEVEL_DEBUG);
        loggerCMD->setCmdLoggerLevel(MegaApi::LOG_LEVEL_DEBUG);
    }

    mutexHistory.init(false);

    ConfigurationManager::loadConfiguration(( argc > 1 ) && !( strcmp(argv[1], "--debug")));

    api = new MegaApi("BdARkQSQ", ConfigurationManager::getConfigFolder().c_str(), "MegaCMD User Agent");
    for (int i = 0; i < 5; i++)
    {
        MegaApi *apiFolder = new MegaApi("BdARkQSQ", (const char*)NULL, "MegaCMD User Agent");
        apiFolders.push(apiFolder);
        apiFolder->setLoggerObject(loggerCMD);
        apiFolder->setLogLevel(MegaApi::LOG_LEVEL_MAX);
        semaphoreapiFolders.release();
    }

    for (int i = 0; i < 100; i++)
    {
        semaphoreClients.release();
    }

    mutexapiFolders.init(false);

    api->setLoggerObject(loggerCMD);
    api->setLogLevel(MegaApi::LOG_LEVEL_MAX);

    cmdexecuter = new MegaCmdExecuter(api, loggerCMD);

    megaCmdGlobalListener = new MegaCmdGlobalListener(loggerCMD);
    megaCmdMegaListener = new MegaCmdMegaListener(api, NULL);
    api->addGlobalListener(megaCmdGlobalListener);
    api->addListener(megaCmdMegaListener);

    // set up the console
#ifdef _WIN32
    console = new CONSOLE_CLASS;
#else
    struct termios term;
    if (tcgetattr(STDIN_FILENO, &term) < 0) //try console
    {
        consoleFailed = true;
        console = NULL;
    }
    else
    {
        console = new CONSOLE_CLASS;
    }
#endif
    cm = new COMUNICATIONMANAGER();


#ifdef _WIN32
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
    signal(SIGINT, sigint_handler);
#endif

    atexit(finalize);

    rl_attempted_completion_function = getCompletionMatches;

    rl_callback_handler_install(NULL, NULL); //this initializes readline somehow,
    // so that we can use rl_message or rl_resize_terminal safely before ever
    // prompting anything.

    if (!ConfigurationManager::session.empty())
    {
        loginInAtStartup = true;
        stringstream logLine;
        logLine << "login " << ConfigurationManager::session;
        LOG_debug << "Executing ... " << logLine.str();
        process_line((char*)logLine.str().c_str());
        loginInAtStartup = false;
    }

    megacmd();
}
