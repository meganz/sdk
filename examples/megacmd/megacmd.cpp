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

#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>

using namespace mega;

MegaApi *api;

MegaCmdExecuter *cmdexecuter;

//api objects for folderlinks
std::queue<MegaApi *> apiFolders;
std::vector<MegaApi *> occupiedapiFolders;
MegaSemaphore semaphoreapiFolders;
MegaMutex mutexapiFolders;

MegaCMDLogger *loggerCMD;

//Syncs
//map<string,sync_struct *> syncsmap;

std::vector<MegaThread *> petitionThreads;

//Comunications Manager
ComunicationsManager * cm;

// global listener
MegaCmdGlobalListener* megaCmdGlobalListener;

//static handle cwd = UNDEF;
//static char *session;

bool loginInAtStartup=false;

vector<string> getlistOfWords(char *ptr);
void insertValidParamsPerCommand(set<string> *validParams, string thecommand); //TODO: place somewhere else
bool setOptionsAndFlags(map<string,string> *opts,map<string,int> *flags,vector<string> *ws, set<string> vvalidOptions, bool global=false);//TODO: place somewhere else

static void store_line(char*);
static void process_line(char *);
static char* line;

// new account signup e-mail address and name
static string signupemail, signupname;

//// signup code being confirmed
static string signupcode;

// password change-related state information
static byte pwkey[SymmCipher::KEYLENGTH];
static byte pwkeybuf[SymmCipher::KEYLENGTH];
static byte newpwkey[SymmCipher::KEYLENGTH];

bool doExit = false;
bool consoleFailed = false;

static char dynamicprompt[128];

static prompttype prompt = COMMAND;

static char pw_buf[256];
static int pw_buf_pos;

// local console
Console* console;

MegaMutex mutexHistory;

#ifdef __linux__
void sigint_handler(int signum)
{
    LOG_verbose << "Received signal: " << signum;
    if (loginInAtStartup) exit(-2);

    rl_replace_line("", 0); //clean contents of actual command
    rl_crlf(); //move to nextline

    // reset position and print prompt
    pw_buf_pos = 0;
    OUTSTREAM << (*dynamicprompt?dynamicprompt:prompts[prompt]) << flush;
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
    strncpy(dynamicprompt,newprompt,sizeof(dynamicprompt));
}

// readline callback - exit if EOF, add to history unless password
static void store_line(char* l)
{
    if (!l)
    {
        doExit = true;
//        OUTSTREAM << "(CTRL+D) Exiting, press RETURN to close ...." << endl;
//        OUTSTREAM << endl;
//        changeprompt("(CTRL+D) Exiting, press RETURN to close ....");
        rl_set_prompt("(CTRL+D) Exiting ...\n");
        return;
    }

    if (*l && prompt == COMMAND)
    {
        mutexHistory.lock();
        add_history(l);
        mutexHistory.unlock();
    }

    line = l;
}

string alocalremotepatterncommands [] = {"put", "sync"};
vector<string> localremotepatterncommands(alocalremotepatterncommands, alocalremotepatterncommands + sizeof alocalremotepatterncommands / sizeof alocalremotepatterncommands[0]);

string aremotepatterncommands[] = {"ls", "cd", "mkdir", "rm", "export", "share"};
vector<string> remotepatterncommands(aremotepatterncommands, aremotepatterncommands + sizeof aremotepatterncommands / sizeof aremotepatterncommands[0]);

string aremoteremotepatterncommands[] = {"mv", "cp"};
vector<string> remoteremotepatterncommands(aremoteremotepatterncommands, aremoteremotepatterncommands + sizeof aremoteremotepatterncommands / sizeof aremoteremotepatterncommands[0]);

string aremotelocalpatterncommands[] = {"get"};
vector<string> remotelocalpatterncommands(aremotelocalpatterncommands, aremotelocalpatterncommands + sizeof aremotelocalpatterncommands / sizeof aremotelocalpatterncommands[0]);

string alocalpatterncommands [] = {"lcd"};
vector<string> localpatterncommands(alocalpatterncommands, alocalpatterncommands + sizeof alocalpatterncommands / sizeof alocalpatterncommands[0]);


string aemailpatterncommands [] = {"invite", "signup"};
vector<string> emailpatterncommands(aemailpatterncommands, aemailpatterncommands + sizeof aemailpatterncommands / sizeof aemailpatterncommands[0]);


//string avalidCommands [] = { "login", "begin", "signup", "confirm", "session", "mount", "ls", "cd", "log", "pwd", "lcd", "lpwd",
//"import", "put", "put", "putq", "get", "get", "get", "getq", "pause", "getfa", "mkdir", "rm", "mv",
//"cp", "sync", "export", "export", "share", "share", "invite", "ipc", "showpcr", "users", "getua",
//"putua", "putbps", "killsession", "whoami", "passwd", "retry", "recon", "reload", "logout", "locallogout",
//"symlink", "version", "debug", "chatf", "chatc", "chati", "chatr", "chatu", "chatga", "chatra", "quit",
//"history" };
string avalidCommands [] = { "login", "session", "mount", "ls", "cd", "log", "pwd", "lcd", "lpwd", "import",
"put", "get", "mkdir", "rm", "mv", "cp", "sync", "export", "share", "invite", "showpcr", "users", "whoami",
"reload", "logout", "version","quit", "history" };
vector<string> validCommands(avalidCommands, avalidCommands + sizeof avalidCommands / sizeof avalidCommands[0]);

bool stringcontained (const char * s, vector<string> list){
    for (int i=0;i<list.size();i++)
        if (list[i] == s)
            return true;
    return false;
}

char * dupstr (char* s) {
  char *r;

  r = (char*) malloc (sizeof(char)*(strlen(s)+1));
  strcpy (r, s);
  return (r);
}

char* generic_completion(const char* text, int state, vector<string> validOptions)
{
    static int list_index, len;
    string name;

    if (!state) {
        list_index = 0;
        len = strlen (text);
    }
    while (list_index < validOptions.size())
    {
        name = validOptions.at(list_index);
        list_index++;

        if (!(strcmp(text,"")) || ( name.size() >=len && strlen(text)>=len && name.find(text) == 0))
                return dupstr((char *)name.c_str());
    }

    return ((char *)NULL);
}

char* commands_completion(const char* text, int state)
{
    return generic_completion(text,state,validCommands);
}

char* local_completion(const char* text, int state)
{
    return ((char *)NULL); //matches will be NULL: readline will use local completion
}

char* empty_completion(const char* text, int state)
{
    // we offer 2 different options so that it doesn't complete (no space is inserted)
    if (state==0) return strdup(" ");
    if (state==1) return strdup(text);
    return NULL;
}

char * flags_completion(const char*text, int state)
{
    vector<string> validparams;

    char *saved_line = rl_copy_text(0, rl_end);
    vector<string> words = getlistOfWords(saved_line);
    if (words.size())
    {
        set<string> setvalidparams;
        setvalidparams.insert("v");//global flags. TODO: they are repeated twice
        setvalidparams.insert("help");

        string thecommand=words[0];
        insertValidParamsPerCommand(&setvalidparams,thecommand);
        set<string>::iterator it;
        for (it = setvalidparams.begin(); it != setvalidparams.end(); it++)
        {
            string param = *it;
            string toinsert;

            if ( param.size() > 1 )
                toinsert = "--"+param;
            else
                toinsert = "-"+param;

            validparams.push_back(toinsert);
        }

    }
    char *toret = generic_completion(text,state,validparams);
    return toret;

}

char * flags_value_completion(const char*text, int state)
{

    vector<string> validValues;

    char *saved_line = rl_copy_text(0, rl_end);
    vector<string> words = getlistOfWords(saved_line);
    if (words.size()>1)
    {
        string thecommand=words[0];
        string currentFlag = words[words.size()-1];

        map<string,string> cloptions;
        map<string,int> clflags;

        set<string> validParams;

        insertValidParamsPerCommand(&validParams, thecommand);

        if (setOptionsAndFlags(&cloptions,&clflags,&words,validParams,true) )
        {
           // return invalid??
        }

        if (thecommand == "share")
        {
            if (currentFlag.find("--level=") == 0 )
            {
                char buf[3];
                sprintf(buf,"%d",MegaShare::ACCESS_UNKNOWN);validValues.push_back(buf);
                sprintf(buf,"%d",MegaShare::ACCESS_READ);validValues.push_back(buf);
                sprintf(buf,"%d",MegaShare::ACCESS_READWRITE);validValues.push_back(buf);
                sprintf(buf,"%d",MegaShare::ACCESS_FULL);validValues.push_back(buf);
                sprintf(buf,"%d",MegaShare::ACCESS_OWNER);validValues.push_back(buf);
            }
            if (currentFlag.find("--with=") == 0 )
            {
                validValues = cmdexecuter->getlistusers();
            }
        }
    }


    char *toret = generic_completion(text,state,validValues);
    return toret;

}

char* remotepaths_completion(const char* text, int state)
{
    string wildtext(text);
    wildtext+="*";
    vector<string> validpaths = cmdexecuter->listpaths(wildtext);
    return generic_completion(text,state,validpaths);
}

char* contacts_completion(const char* text, int state)
{
    vector<string> validcontacts = cmdexecuter->getlistusers();
    return generic_completion(text,state,validcontacts);
}

void discardOptionsAndFlags(vector<string> *ws)
{
    for(std::vector<string>::iterator it = ws->begin(); it != ws->end();) {
        /* std::cout << *it; ... */
        string w = (string)*it;
        if (w.length() && w.at(0)=='-') //begins with "-"
        {
            it=ws->erase(it);
        }
        else //not an option/flag
        {
            ++it;
        }
    }
}

rl_compentry_func_t *getCompletionFunction (vector<string> words)
{
    // Strip words without flags
    string thecommand = words[0];

    if (words.size() > 1)
    {
        string lastword = words[words.size()-1];
        if (lastword.find_first_of("-") == 0)
        {
            if (lastword.find_last_of("=") != string::npos)
//            if (lastword.find_last_of("=") == lastword.size()-1)
                return flags_value_completion;
            else
                return flags_completion;
        }
    }
    discardOptionsAndFlags(&words);

    int currentparameter = words.size()-1;
    if (stringcontained(thecommand.c_str(),localremotepatterncommands))
    {
        if (currentparameter==1)
            return local_completion;
        if (currentparameter==2)
            return remotepaths_completion;
    }
    else if (stringcontained(thecommand.c_str(),remotepatterncommands))
    {
        if (currentparameter==1)
            return remotepaths_completion;
    }
    else if (stringcontained(thecommand.c_str(),localpatterncommands))
    {
        if (currentparameter==1)
            return local_completion;
    }
    else if (stringcontained(thecommand.c_str(),remoteremotepatterncommands))
    {
        if (currentparameter==1 || currentparameter==2)
            return remotepaths_completion;
    }
    else if (stringcontained(thecommand.c_str(),remotelocalpatterncommands))
    {
        if (currentparameter==1)
            return remotepaths_completion;
        if (currentparameter==2)
            return local_completion;
    }
    else if (stringcontained(thecommand.c_str(),emailpatterncommands))
    {
        if (currentparameter==1)
            return contacts_completion;
    }

    return empty_completion;
}

static char** getCompletionMatches( const char * text , int start,  int end)
{
    char **matches;

    matches = (char **)NULL;

    if (start == 0)
    {
        matches = rl_completion_matches ((char*)text, &commands_completion);
        if (matches == NULL) matches = rl_completion_matches ((char*)text, &empty_completion);
    }
    else
    {
        char *saved_line = rl_copy_text(0, rl_end);
        vector<string> words = getlistOfWords(saved_line);
        if (strlen(saved_line) && saved_line[strlen(saved_line)-1]==' ') words.push_back("");

        matches = rl_completion_matches ((char*)text, getCompletionFunction(words));
        free(saved_line);
    }
    return (matches);
}


void printHistory()
{
//    //TODO: this might need to be protected betweeen mutex
    int length = history_length;
    int offset =1;
    int rest=length;
    while(rest>=10) {offset++;rest=rest/10;}

    mutexHistory.lock();
    for(int i = 0; i < length; i++) {
      history_set_pos(i);
      OUTSTREAM << setw(offset) <<  i << "  " << current_history()->line << endl;
    }
    mutexHistory.unlock();
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
//    if(!strcmp(command,"export") ) return "export remotepath [expireTime|del]";
    if(!strcmp(command,"export") ) return "export [-d|-a [--expire=TIMEDELAY]] [remotepath]";
//    if(!strcmp(command,"share") ) return "share [remotepath [dstemail [r|rw|full] [origemail]]]";
    if(!strcmp(command,"share") ) return "share [-p] [-d|-a --with=user@email.com [--level=LEVEL]] [remotepath]";
    if(!strcmp(command,"invite") ) return "invite dstemail [origemail|del|rmd]";
    if(!strcmp(command,"ipc") ) return "ipc handle a|d|i";
    if(!strcmp(command,"showpcr") ) return "showpcr";
    if(!strcmp(command,"users") ) return "users [-s]";
    if(!strcmp(command,"getua") ) return "getua attrname [email]";
    if(!strcmp(command,"putua") ) return "putua attrname [del|set string|load file]";
    if(!strcmp(command,"putbps") ) return "putbps [limit|auto|none]";
    if(!strcmp(command,"killsession") ) return "killsession [all|sessionid]";
    if(!strcmp(command,"whoami") ) return "whoami [-l]";
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
    if(!strcmp(command,"history") ) return "history";
    return "command not found";
}

bool validCommand(string thecommand){
    return stringcontained((char *)thecommand.c_str(),validCommands);
//    return getUsageStr(thecommand.c_str()) != "command not found";
}

void printAvailableCommands()
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
}

string getHelpStr(const char *command)
{
    ostringstream os;

    os << getUsageStr(command) << endl;
    if(!strcmp(command,"login") )
    {
        os << "Logs in. Either with email and password, with session ID, or into an exportedfolder";
        os << " If login into an exported folder indicate url#key" << endl;
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
        os << " -c" << "\t" << "CMD log level (higher level messages). Messages captured by the command line." << endl;
        os << " -s" << "\t" << "SDK log level (lower level messages). Messages captured by the engine and libs" << endl;
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
        os << endl;
        os << "Options:" << endl;
        os << " -p" << "\t" << "Allow recursive" << endl;
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
    else if(!strcmp(command,"export") )
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
        os << "If a remote path is given it'll be used to add/delete or in case of no option selected,"<< endl;
        os << " it will display all the exports existing in the tree of that path" << endl;
    }
    else if(!strcmp(command,"share") )
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
        os << "              " << "\t" << "3: " << "Owner access" << endl; //TODO: check this. also add value validation
        os << endl;
        os << "If a remote path is given it'll be used to add/delete or in case of no option selected,"<< endl;
        os << " it will display all the shares existing in the tree of that path" << endl;
    }
//    if(!strcmp(command,"invite") ) return "invite dstemail [origemail|del|rmd]";
//    if(!strcmp(command,"ipc") ) return "ipc handle a|d|i";
//    if(!strcmp(command,"showpcr") ) return "showpcr";
    else if(!strcmp(command,"users") )
    {
        os << "List contacts" << endl;
        os << endl;
        os << "Options:" << endl;
        os << " -s" << "\t" << "Show shared folders" << endl;
    }
//    if(!strcmp(command,"getua") ) return "getua attrname [email]";
//    if(!strcmp(command,"putua") ) return "putua attrname [del|set string|load file]";
//    if(!strcmp(command,"putbps") ) return "putbps [limit|auto|none]";
//    if(!strcmp(command,"killsession") ) return "killsession [all|sessionid]";
    else if(!strcmp(command,"whoami") )
    {
        os << "Print info of the user" << endl;
        os << endl;
        os << "Options:" << endl;
        os << " -l" << "\t" << "Show extended info: total storage used, storage per main folder (see mount), pro level, account balance, and also the active sessions" << endl;
    }
//    if(!strcmp(command,"passwd") ) return "passwd";
//    if(!strcmp(command,"retry") ) return "retry";
//    if(!strcmp(command,"recon") ) return "recon";
    else if(!strcmp(command,"reload") )
    {
        os << "Forces a reload of the remote files of the user" << endl;
    }
//    if(!strcmp(command,"locallogout") ) return "locallogout";
//    if(!strcmp(command,"symlink") ) return "symlink";
    else if(!strcmp(command,"version") )
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
    else if(!strcmp(command,"quit") )
    {
        os << "Quits" << endl;
        os << endl;
        os << "Notice that the session will still be active, and local caches available" << endl;
        os << "The session will be resumed when the service is restarted" << endl;
    }


    return os.str();
}


bool setOptionsAndFlags(map<string,string> *opts,map<string,int> *flags,vector<string> *ws, set<string> vvalidOptions, bool global)
{
    bool discarded = false;

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
            else //option=value
            {

                string cleared = ltrim(w,'-');
                size_t p=cleared.find_first_of("=");
                string optname = cleared.substr(0,p);
                if (vvalidOptions.find(optname) !=vvalidOptions.end())
                {
                    string value = cleared.substr(p+1);

                    value=rtrim(ltrim(value,'"'),'"');
                    (*opts)[optname] = value;
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

void insertValidParamsPerCommand(set<string> *validParams, string thecommand){
    if ("ls" == thecommand)
    {
        validParams->insert("R");
        validParams->insert("r");
        validParams->insert("l");
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
        validParams->insert("expire");
    }
    else if ("share" == thecommand)
    {
        validParams->insert("a");
        validParams->insert("d");
        validParams->insert("p");
        validParams->insert("with");
        validParams->insert("level");
        validParams->insert("personal-representation");
    }
    else if ("mkdir" == thecommand)
    {
        validParams->insert("p");
    }
    else if ("users" == thecommand)
    {
        validParams->insert("s");
    }
    else if ("invite" == thecommand)
    {
        validParams->insert("d");
        validParams->insert("r");
        validParams->insert("message");
    }
}


vector<string> getlistOfWords(char *ptr)
{
    vector<string> words;

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
                if (*ptr == '"')
                {
                    while(*++ptr != '"' && *ptr!='\0') { }
                }
                ptr++;
            }

            words.push_back(string(wptr, ptr - wptr));
        }
    }
    return words;
}

void executecommand(char* ptr){

    vector<string> words = getlistOfWords(ptr);
    if (!words.size())
    {
        return;
    }

    string thecommand = words[0];

    if (thecommand == "?" || thecommand == "h" || thecommand == "help")
    {
        printAvailableCommands();
        return;
    }

    map<string,string> cloptions;
    map<string,int> clflags;

    string validGlobalParameters[]={"v","help"};
    set<string> validParams(validGlobalParameters,validGlobalParameters + sizeof(validGlobalParameters)/sizeof(*validGlobalParameters));

    if (setOptionsAndFlags(&cloptions,&clflags,&words,validParams,true) )
    {
        OUTSTREAM << "      " << getUsageStr(thecommand.c_str()) << endl;
    }

    insertValidParamsPerCommand(&validParams, thecommand);

    if (!validCommand(thecommand)) { //unknown command
        OUTSTREAM << "      " << getUsageStr(thecommand.c_str()) << endl;
        return;
    }

    if (setOptionsAndFlags(&cloptions,&clflags,&words,validParams) )
    {
        OUTSTREAM << "      " << getUsageStr(thecommand.c_str()) << endl;
        return;
    }
    setCurrentThreadLogLevel(MegaApi::LOG_LEVEL_ERROR+getFlag(&clflags,"v"));

    if(getFlag(&clflags,"help")) {
        string h = getHelpStr(thecommand.c_str()) ;
         OUTSTREAM << h << endl;
         return;
    }
    cmdexecuter->executecommand(words, clflags, cloptions);
}


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
                 cmdexecuter->loginWithPassword(l);

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
//                store_line(NULL);
                exit(0);
            }
            executecommand(l);

        break;
    }
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

        cmdexecuter->nodepath(h, &path);

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
    setCurrentOutCode(0);

    LOG_verbose << " Processing " << inf->line << " in thread: " << getCurrentThread()
              << " socket output: " <<  inf->outSocket ;

    process_line(inf->line);

    LOG_verbose << " Procesed " << inf->line << " in thread: " << getCurrentThread()
              << " socket output: " <<  inf->outSocket ;

    LOG_verbose << "Output to write in socket " <<inf->outSocket << ": <<" << s.str() << ">>";

    cm->returnAndClosePetition(inf,&s, getCurrentOutCode());

    return NULL;
}


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
    if (!consoleFailed)
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
    delete cmdexecuter;

    OUTSTREAM << "resources have been cleaned ..."  << endl;
}

// main loop
void megacmd()
{
    char *saved_line = NULL;
    int saved_point = 0;

    rl_save_prompt();

    int readline_fd = fileno(rl_instream);

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

        // command editing loop - exits when a line is submitted
        for (;;)
        {
            if (Waiter::HAVESTDIN)
            {
                if (prompt == COMMAND)
                {
                    if (consoleFailed)
                        cm->waitForPetition();
                    else
                        cm->waitForPetitionOrReadlineInput(readline_fd);

                    if (cm->receivedReadlineInput(readline_fd)) {
                        rl_callback_read_char();
                        if (doExit) exit(0);
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
        if (doExit) exit(0);

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
    SimpleLogger::setLogLevel(logMax); // do not filter anything here, log level checking is done by loggerCMD

    mutexHistory.init(false);

    ConfigurationManager::loadConfiguration();

    api=new MegaApi("BdARkQSQ",ConfigurationManager::getConfigFolder().c_str(), "MegaCMD User Agent");
    for (int i=0;i<5;i++)
    {
        MegaApi *apiFolder=new MegaApi("BdARkQSQ",(const char*)NULL, "MegaCMD User Agent");
        apiFolders.push(apiFolder);
        apiFolder->setLoggerObject(loggerCMD);
        apiFolder->setLogLevel(MegaApi::LOG_LEVEL_MAX);
        semaphoreapiFolders.release();
    }
    mutexapiFolders.init(false);

    loggerCMD = new MegaCMDLogger(&cout);

    loggerCMD->setApiLoggerLevel(MegaApi::LOG_LEVEL_ERROR);
    loggerCMD->setCmdLoggerLevel(MegaApi::LOG_LEVEL_DEBUG);

    api->setLoggerObject(loggerCMD);
    api->setLogLevel(MegaApi::LOG_LEVEL_MAX);

    cmdexecuter = new MegaCmdExecuter(api, loggerCMD);

    megaCmdGlobalListener =  new MegaCmdGlobalListener(loggerCMD);
    api->addGlobalListener(megaCmdGlobalListener);

    // set up the console
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

    cm = new ComunicationsManager();

#ifdef __linux__
    // prevent CTRL+C exit
    signal(SIGINT, sigint_handler);
#endif

    atexit(finalize);

    rl_attempted_completion_function = getCompletionMatches;

    rl_callback_handler_install(NULL,NULL); //this initializes readline somehow,
            // so that we can use rl_message or rl_resize_terminal safely before ever
            // prompting anything.

    if (!ConfigurationManager::session.empty())
    {
        loginInAtStartup = true;
        stringstream logLine;
        logLine << "login " << ConfigurationManager::session;
        LOG_debug << "Executing ... " << logLine.str();
        process_line((char *)logLine.str().c_str());
        loginInAtStartup = false;
    }

    megacmd();
}

#endif //linux
