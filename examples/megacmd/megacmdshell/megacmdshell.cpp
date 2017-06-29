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

#ifndef _WIN32
#include <signal.h>
#include <sys/types.h>
#endif

#if defined(_WIN32) || defined(_WIN64)
  #define snprintf _snprintf
  #define vsnprintf _vsnprintf
  #define strcasecmp _stricmp
  #define strncasecmp _strnicmp
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

string getCurrentLine()
{
    char *saved_line = rl_copy_text(0, rl_point);
    string toret(saved_line);
    free(saved_line);
    saved_line = NULL;
    return toret;
}

void sleepSeconds(int seconds)
{
#ifdef _WIN32
    Sleep(1000*seconds);
#else
    sleep(seconds);
#endif
}

void sleepMicroSeconds(long microseconds)
{
#ifdef _WIN32
    Sleep(microseconds);
#else
    usleep(microseconds*1000);
#endif
}

// end utily functions


// Console related functions:
void console_readpwchar(char* pw_buf, int pw_buf_size, int* pw_buf_pos, char** line)
{
#ifdef _WIN32
    char c;
      DWORD cread;

      if (ReadConsole(GetStdHandle(STD_INPUT_HANDLE), &c, 1, &cread, NULL) == 1)
      {
          if ((c == 8) && *pw_buf_pos)
          {
              (*pw_buf_pos)--;
          }
          else if (c == 13)
          {
              *line = (char*)malloc(*pw_buf_pos + 1);
              memcpy(*line, pw_buf, *pw_buf_pos);
              (*line)[*pw_buf_pos] = 0;
          }
          else if (*pw_buf_pos < pw_buf_size)
          {
              pw_buf[(*pw_buf_pos)++] = c;
          }
      }
#else
    // FIXME: UTF-8 compatibility

    char c;

    if (read(STDIN_FILENO, &c, 1) == 1)
    {
        if (c == 8 && *pw_buf_pos)
        {
            (*pw_buf_pos)--;
        }
        else if (c == 13)
        {
            *line = (char*) malloc(*pw_buf_pos + 1);
            memcpy(*line, pw_buf, *pw_buf_pos);
            (*line)[*pw_buf_pos] = 0;
        }
        else if (*pw_buf_pos < pw_buf_size)
        {
            pw_buf[(*pw_buf_pos)++] = c;
        }
    }
#endif
}
void console_setecho(bool echo)
{
#ifdef _WIN32
    HANDLE hCon = GetStdHandle(STD_INPUT_HANDLE);
    DWORD mode;

    GetConsoleMode(hCon, &mode);

    if (echo)
    {
        mode |= ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT;
    }
    else
    {
        mode &= ~(ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT);
    }

    SetConsoleMode(hCon, mode);
#else
    //do nth
#endif
}

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

// convert Windows Unicode to UTF-8
void utf16ToUtf8(const wchar_t* utf16data, int utf16size, string* utf8string)
{
    if(!utf16size)
    {
        utf8string->clear();
        return;
    }

    utf8string->resize((utf16size + 1) * 4);

    utf8string->resize(WideCharToMultiByte(CP_UTF8, 0, utf16data,
        utf16size,
        (char*)utf8string->data(),
        utf8string->size() + 1,
        NULL, NULL));
}
#endif

// password change-related state information
string oldpasswd;
string newpasswd;

bool doExit = false;

bool handlerinstalled = false;

bool requirepromptinstall = true;

bool procesingline = false;

static char dynamicprompt[128];

static char* line;

static prompttype prompt = COMMAND;

static char pw_buf[256];
static int pw_buf_pos;

string loginname;
string linktoconfirm;

bool confirminglink = false;

// communications with megacmdserver:
MegaCmdShellCommunications *comms;

std::mutex mutexPrompt;

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
  cerr << "Reached CtrlHandler: " << fdwCtrlType << endl;

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
        console_setecho(true);
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
        console_setecho(false);
    }
}

// readline callback - exit if EOF, add to history unless password
static void store_line(char* l)
{
    procesingline = true;
    if (!l)
    {
#ifndef _WIN32 // to prevent exit with Supr key
        doExit = true;
        rl_set_prompt("(CTRL+D) Exiting ...\n");
        if (comms->serverinitiatedfromshell)
        {
            cerr << " Forwarding exit command to the server, since this cmd shell (most likely) initiated it" << endl;
            comms->executeCommand("exit");
        }
#endif
        return;
    }

    if (*l && ( prompt == COMMAND ))
    {
        add_history(l);
    }

    line = l;
}

#ifdef _WIN32
//widechar to utf8 string
void localwtostring(const std::wstring* wide, std::string *multibyte)
{
    if( !wide->empty() )
    {
        int size_needed = WideCharToMultiByte(CP_UTF8, 0, wide->data(), (int)wide->size(), NULL, 0, NULL, NULL);
        multibyte->resize(size_needed);
        WideCharToMultiByte(CP_UTF8, 0, wide->data(), (int)wide->size(), (char*)multibyte->data(), size_needed, NULL, NULL);
    }
}

bool validoptionforreadline(const string& string)
{// TODO: this has not been tested in 100% cases (perhaps it is too diligent or too strict)
    int c,i,ix,n,j;
    for (i=0, ix=string.length(); i < ix; i++)
    {
        c = (unsigned char) string[i];

        //if (c>0xC0) return false;
        //if (c==0x09 || c==0x0a || c==0x0d || (0x20 <= c && c <= 0x7e) ) n = 0; // is_printable_ascii
        if (0x00 <= c && c <= 0x7f) n=0; // 0bbbbbbb
        else if ((c & 0xE0) == 0xC0) n=1; // 110bbbbb
        else if ( c==0xed && i<(ix-1) && ((unsigned char)string[i+1] & 0xa0)==0xa0) return false; //U+d800 to U+dfff
        else if ((c & 0xF0) == 0xE0) {return false; n=2;} // 1110bbbb
        else if ((c & 0xF8) == 0xF0) {return false; n=3;} // 11110bbb
        //else if (($c & 0xFC) == 0xF8) n=4; // 111110bb //byte 5, unnecessary in 4 byte UTF-8
        //else if (($c & 0xFE) == 0xFC) n=5; // 1111110b //byte 6, unnecessary in 4 byte UTF-8
        else return false;
        for (j=0; j<n && i<ix; j++) { // n bytes matching 10bbbbbb follow ?
            if ((++i == ix) || (( (unsigned char)string[i] & 0xC0) != 0x80))
                return false;
        }



    }
    return true;
}


bool validwcharforeadline(const wchar_t thewchar)
{
    wstring input;
    input+=thewchar;
    string output;
    localwtostring(&input,&output);
    return validoptionforreadline(output);

}

wstring escapereadlinebreakers(const wchar_t *what)
{
    wstring output;
    for( u_int i = 0; i < wcslen( what ) ; i++ )
    {
        if(validwcharforeadline(what[ i ] ))
        {
            output.reserve( output.size() + 1 );
            output += what[ i ];
        } else {
            wchar_t code[ 7 ];
            swprintf( code, 7, L"\\u%0.4X", what[ i ] ); //while this does not work (yet) as what, at least it shows something and does not break
            //TODO: ideally we would do the conversion from escaped unicode chars \uXXXX back to wchar_t in the server
            // NOTICE: I was able to execute a command with a literl \x242ee (which correspond to \uD850\uDEEE in UTF16).
            // So it'll be more interesting to output here the complete unicode char and in unescapeutf16escapedseqs revert it.
            //     or keep here the UTF16 escaped secs and revert them correctly in the unescapeutf16escapedseqs
            output.reserve( output.size() + 7 ); // "\u"(2) + 5(uint max digits capacity)
            output += code;
        }
    }
    return output;
}

#endif

void install_rl_handler(const char *theprompt)
{
#ifdef _WIN32
    wstring wswhat;
    stringtolocalw(theprompt,&wswhat);
    const wchar_t *what = wswhat.c_str();


    // escape characters that break readline input (e.g. Chinese ones. e.g \x242ee)
    wstring output = escapereadlinebreakers(what);

    // give readline something it understands
    what = output.c_str();
    size_t buffer_size;
    wcstombs_s(&buffer_size, NULL, 0, what, _TRUNCATE);

    if (buffer_size) //coversion is ok
    {
        // do the actual conversion
        char *buffer = new char[buffer_size];
        wcstombs_s(&buffer_size, buffer, buffer_size,what, _TRUNCATE);
        rl_callback_handler_install(buffer, store_line);
    }
    else
    {
        rl_callback_handler_install("INVALID_PROMPT: ", store_line);
    }

#else
    rl_callback_handler_install(theprompt, store_line);
#endif
}

void changeprompt(const char *newprompt, bool redisplay)
{
    mutexPrompt.lock();
    strncpy(dynamicprompt, newprompt, sizeof( dynamicprompt ));

    if (redisplay)
    {
        // save line
        int saved_point = rl_point;
        char *saved_line = rl_copy_text(0, rl_end);

        rl_clear_message();

        // enter a new line if not processing sth (otherwise, the newline should already be there)
        if (!procesingline)
        {
            rl_crlf();
        }

        install_rl_handler(*dynamicprompt ? dynamicprompt : prompts[COMMAND]);

        // restore line
        if (saved_line)
        {
            rl_replace_line(saved_line, 0);
            free(saved_line);
            saved_line = NULL;
        }
        rl_point = saved_point;
        rl_redisplay();

        handlerinstalled = true;

        requirepromptinstall = false;
    }
    mutexPrompt.unlock();
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

void pushvalidoption(vector<string>  *validOptions, const char *beginopt)
{
#ifdef _WIN32
    if (validoptionforreadline(beginopt))
    {
        validOptions->push_back(beginopt);
    }
    else
    {
        wstring input;
        stringtolocalw(beginopt,&input);
        wstring output = escapereadlinebreakers(input.c_str());

        string soutput;
        localwtostring(&output,&soutput);
        validOptions->push_back(soutput.c_str());
    }
#else
    validOptions->push_back(beginopt);
#endif
}


char* remote_completion(const char* text, int state)
{
    char *saved_line = strdup(getCurrentLine().c_str());

    static vector<string> validOptions;
    if (state == 0)
    {
        validOptions.clear();
        string completioncommand("completionshell ");
        completioncommand+=saved_line;

        OUTSTRING s;
        OUTSTRINGSTREAM oss(s);

        comms->executeCommand(completioncommand,oss);

        string outputcommand;

#ifdef _WIN32
        localwtostring(&oss.str(),&outputcommand);
#else
         outputcommand = oss.str();
#endif

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
                    pushvalidoption(&validOptions,beginopt);
                }

                beginopt=ptr+1;
            }
            ptr++;
        }
        if (*beginopt && strcmp(beginopt," "))
        {
            pushvalidoption(&validOptions,beginopt);
        }
    }

    free(saved_line);
    saved_line = NULL;

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

    for (int i = 0; i < length; i++)
    {
        history_set_pos(i);
        OUTSTREAM << setw(offset) << i << "  " << current_history()->line << endl;
    }
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
        utf16ToUtf8(wbuffer, wbuffer_size,&receivedutf8);

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
                cerr << "Error at select at wait_for_input errno: " << errno << endl;
            return;
        }
    }
}


vector<string> getlistOfWords(char *ptr, bool ignoreTrailingSpaces = true)
{
    vector<string> words;

    char* wptr;

    // split line into words with quoting and escaping
    for (;; )
    {
        // skip leading blank space
        while (*ptr > 0 && *ptr <= ' ' && (ignoreTrailingSpaces || *(ptr+1)))
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
            while (*ptr == ' ') ptr++;// only possible if ptr+1 is the end

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


void process_line(char * line)
{
    switch (prompt)
    {
        case AREYOUSURE:
            //this is currently never used
            if (!strcasecmp(line,"yes") || !strcasecmp(line,"y"))
            {
                comms->setResponseConfirmation(true);
                setprompt(COMMAND);
            }
            else if (!strcasecmp(line,"no") || !strcasecmp(line,"n"))
            {
                comms->setResponseConfirmation(false);
                setprompt(COMMAND);
            }
            else
            {
                //Do nth, ask again
                OUTSTREAM << "Please enter: [y]es/[n]o: " << flush;
            }
            break;
        case LOGINPASSWORD:
        {
            if (!strlen(line))
            {
                break;
            }
            if (!confirminglink)
            {
                string logincommand("login -v ");
                logincommand+=loginname;
                logincommand+=" " ;
                logincommand+=line;

                comms->executeCommand(logincommand.c_str());
            }
            else
            {
                string confirmcommand("confirm ");
                confirmcommand+=linktoconfirm;
                confirmcommand+=" " ;
                confirmcommand+=loginname;
                confirmcommand+=" " ;
                confirmcommand+=line;

                comms->executeCommand(confirmcommand.c_str());

                confirminglink = false;
            }

            setprompt(COMMAND);
            break;
        }

        case OLDPASSWORD:
        {
            if (!strlen(line))
            {
                break;
            }
            oldpasswd = line;
            OUTSTREAM << endl;
            setprompt(NEWPASSWORD);
            break;
        }

        case NEWPASSWORD:
        {
            if (!strlen(line))
            {
                break;
            }
            newpasswd = line;
            OUTSTREAM << endl;
            setprompt(PASSWORDCONFIRM);
        }
            break;

        case PASSWORDCONFIRM:
        {
            if (!strlen(line))
            {
                break;
            }
            if (line != newpasswd)
            {
                OUTSTREAM << endl << "New passwords differ, please try again" << endl;
            }
            else
            {
                OUTSTREAM << endl;
                string changepasscommand("passwd ");
                changepasscommand+=oldpasswd;
                changepasscommand+=" " ;
                changepasscommand+=newpasswd;

                comms->executeCommand(changepasscommand.c_str());
            }

            setprompt(COMMAND);
            break;
        }
        case COMMAND:
        {
            vector<string> words = getlistOfWords(line);
            if (words.size())
            {
                if (words[0] == "history")
                {
                    printHistory();
                }
#ifdef _WIN32
                else if (words[0] == "unicode" && words.size() == 1)
                {
                    rl_getc_function=(rl_getc_function==&getcharacterreadlineUTF16support)?rl_getc:&getcharacterreadlineUTF16support;
                    OUTSTREAM << "Unicode shell input " << ((rl_getc_function==&getcharacterreadlineUTF16support)?"ENABLED":"DISABLED") << endl;
                    return;
                }
#endif
                else if (words[0] == "passwd")
                {

                    //if (api->isLoggedIn()) //TODO: this sould be asked to the server or managed as a status
                    //{
                    if (words.size() == 1)
                    {
                        setprompt(OLDPASSWORD);
                    }
                    else
                    {
                        comms->executeCommand(line);
                    }
                    //}
                    //else
                    //{
                    //    setCurrentOutCode(MCMD_NOTLOGGEDIN);
                    //    LOG_err << "Not logged in.";
                    //}

                    return;
                }
                else if (words[0] == "login")
                {
                    //if (!api->isLoggedIn()) //TODO: this sould be asked to the server or managed as a status
                    //{
                        if (words.size() == 2)
                        {
                            loginname = words[1];
                            setprompt(LOGINPASSWORD);
                        }
                        else
                        {
                            comms->executeCommand(line);
                        }
                    //}
                    //else
                    //{
                    //    setCurrentOutCode(MCMD_INVALIDSTATE);
                    //    LOG_err << "Already logged in. Please log out first.";
                    //}
                }
                else if (words[0] == "confirm")
                {
                    if (words.size() == 3)
                    {
                        linktoconfirm = words[1];
                        loginname = words[2];
                        confirminglink = true;
                        setprompt(LOGINPASSWORD);
                    }
                    else
                    {
                        comms->executeCommand(line);
                    }
                }
                else if ( words[0] == "clear" )
                {
#ifdef _WIN32
                    HANDLE hStdOut;
                    CONSOLE_SCREEN_BUFFER_INFO csbi;
                    DWORD count;

                    hStdOut = GetStdHandle( STD_OUTPUT_HANDLE );
                    if (hStdOut == INVALID_HANDLE_VALUE) return;

                    /* Get the number of cells in the current buffer */
                    if (!GetConsoleScreenBufferInfo( hStdOut, &csbi )) return;
                    /* Fill the entire buffer with spaces */
                    if (!FillConsoleOutputCharacter( hStdOut, (TCHAR) ' ', csbi.dwSize.X *csbi.dwSize.Y, { 0, 0 }, &count ))
                    {
                        return;
                    }
                    /* Fill the entire buffer with the current colors and attributes */
                    if (!FillConsoleOutputAttribute(hStdOut, csbi.wAttributes, csbi.dwSize.X *csbi.dwSize.Y, { 0, 0 }, &count))
                    {
                        return;
                    }
                    /* Move the cursor home */
                    SetConsoleCursorPosition( hStdOut, { 0, 0 } );
#else
                    rl_clear_screen(0,0);
#endif
                    return;
                }
                else
                {
                    // execute user command
                    comms->executeCommand(line);
                }
            }
            else
            {
                cerr << "failed to interprete input line: " << line << endl;
            }
            break;
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

    readline_fd = fileno(rl_instream);

    static bool firstloop = true;

    comms->registerForStateChanges();

    //give it a while to communicate the state
    sleepMicroSeconds(1);

#ifdef _WIN32
    // due to a failure in reconnecting to the socket, if the server was initiated in while registeringForStateChanges
    // in windows we would not be yet connected. we need to manually try to register again.
    if (comms->registerAgainRequired)
    {
        comms->registerForStateChanges();
    }
    //give it a while to communicate the state
    sleepMicroSeconds(1);
#endif

    for (;; )
    {
        if (prompt == COMMAND)
        {
            mutexPrompt.lock();
            if (requirepromptinstall)
            {
                install_rl_handler(*dynamicprompt ? dynamicprompt : prompts[COMMAND]);
                handlerinstalled = false;

                // display prompt
                if (saved_line)
                {
                    rl_replace_line(saved_line, 0);
                    free(saved_line);
                    saved_line = NULL;
                }

                rl_point = saved_point;
                rl_redisplay();
            }
            mutexPrompt.unlock();
        }

        firstloop = false;


        // command editing loop - exits when a line is submitted
        for (;; )
        {
            if (prompt == COMMAND || prompt == AREYOUSURE)
            {
                procesingline = false;
                wait_for_input(readline_fd);

                //api->retryPendingConnections(); //TODO: this should go to the server!

                rl_callback_read_char(); //this calls store_line if last char was enter

                if (doExit)
                {
                    return;
                }
            }
            else
            {
                console_readpwchar(pw_buf, sizeof pw_buf, &pw_buf_pos, &line);
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
                mutexPrompt.lock();
                process_line(line);
                requirepromptinstall = true;
                mutexPrompt.unlock();

                if (comms->registerAgainRequired)
                {
                    // register again for state changes
                     comms->registerForStateChanges();
                     comms->registerAgainRequired = false;
                }

                // sleep, so that in case there was a changeprompt waiting, gets executed before relooping
                // this is not 100% guaranteed to happen
                sleepSeconds(0);

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
            saved_line = NULL;
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
    for (int i=1; i <= num_matches; i++) //contrary to what the documentation says, num_matches is not the size of c (but num_matches+1), current text is preappended in c[0]
    {
        OUTSTREAM << setw(max_length+1) << left << c[i];
        if ( (i%nelements_per_col == 0) && (i != num_matches))
        {
            OUTSTREAM << endl;
        }
    }
    OUTSTREAM << endl;
}
#endif

bool readconfirmationloop(const char *question)
{
    bool firstime = true;
    for (;; )
    {
        string response;

        if (firstime)
        {
            response = readline(question);

        }
        else
        {
            response = readline("Please enter [y]es/[n]o:");

        }

        firstime = false;

        if (response == "yes" || response == "y" || response == "YES" || response == "Y")
        {
            rl_callback_handler_remove();
            return true;
        }
        if (response == "no" || response == "n" || response == "NO" || response == "N")
        {
            rl_callback_handler_remove();
            return false;
        }
    }
}


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

    // intialize the comms object
    comms = new MegaCmdShellCommunications();

#if _WIN32
    if( SetConsoleCtrlHandler( (PHANDLER_ROUTINE) CtrlHandler, TRUE ) )
     {
        //cerr << "Control handler set" << endl; //TODO: delete
     }
     else
     {
        cerr << "Control handler set failed" << endl;
     }
#else
    // prevent CTRL+C exit
    signal(SIGINT, sigint_handler);
#endif

//    atexit(finalize);//TODO: reset?

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

    readloop();
//    finalize(); //TODO: reset?
}
