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


// utility functions
char * dupstr(char* s)
{
    char *r;

    r = (char*)malloc(sizeof( char ) * ( strlen(s) + 1 ));
    strcpy(r, s);
    return( r );
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

string getCurrentThreadLine() //TODO: rename to sth more sensefull
{
    char *saved_line = rl_copy_text(0, rl_point);
    string toret(saved_line);
    free(saved_line);
    return toret;

}
// end utily functions


// UNICODE SUPPORT FOR WINDOWS
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

//TODO: add 'unicode' command in megacmdshell

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

void printWelcomeMsg();

void sigint_handler(int signum)
{
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
                if (strcmp(beginopt," ")) //the server will give a " " for empty_completion (no matches)
                {
                    validOptions.push_back(beginopt);
                }

                beginopt=ptr+1;
            }
            ptr++;
        }
        if (*beginopt && strcmp(beginopt," "))
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

    matches = rl_completion_matches((char*)text, remote_completion);

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
void readloop()
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
                if (!strcmp(line,"history"))
                {
                    printHistory();
                }
                else
                {
                    // execute user command
                    comms->executeCommand(line);
                }
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

    readloop();
//    finalize(); //TODO: reset?
}
