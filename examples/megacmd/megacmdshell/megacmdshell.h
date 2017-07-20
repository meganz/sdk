#ifndef MEGACMDSHELL_H
#define MEGACMDSHELL_H

#include <iostream>
#include <string>

#ifdef _WIN32

#define OUTSTREAMTYPE std::wostream
#define OUTSTRINGSTREAM std::wostringstream
#define OUTSTRING std::wstring
#define COUT std::wcout

std::wostream & operator<< ( std::wostream & ostr, std::string const & str );
std::wostream & operator<< ( std::wostream & ostr, const char * str );
std::ostringstream & operator<< ( std::ostringstream & ostr, std::wstring const &str);

#else
#define OUTSTREAMTYPE std::ostream
#define OUTSTRINGSTREAM std::ostringstream
#define OUTSTRING std::string
#define COUT std::cout
#endif

#define OUTSTREAM COUT

enum prompttype
{
    COMMAND, LOGINPASSWORD, OLDPASSWORD, NEWPASSWORD, PASSWORDCONFIRM, AREYOUSURE
};

static const char* const prompts[] =
{
    "MEGA CMD> ", "Password:", "Old Password:", "New Password:", "Retype New Password:", "Are you sure to delete? "
};

//enum
//{
//    MCMD_OK = 0,              ///< Everything OK

//    MCMD_EARGS = -51,         ///< Wrong arguments
//    MCMD_INVALIDEMAIL = -52,  ///< Invalid email
//    MCMD_NOTFOUND = -53,      ///< Resource not found
//    MCMD_INVALIDSTATE = -54,  ///< Invalid state
//    MCMD_INVALIDTYPE = -55,   ///< Invalid type
//    MCMD_NOTPERMITTED = -56,  ///< Operation not allowed
//    MCMD_NOTLOGGEDIN = -57,   ///< Needs loging in
//    MCMD_NOFETCH = -58,       ///< Nodes not fetched
//    MCMD_EUNEXPECTED = -59,   ///< Unexpected failure

//    MCMD_REQCONFIRM = -60,     ///< Confirmation required

//};

void sleepSeconds(int seconds);

void sleepMicroSeconds(long microseconds);

void restoreprompt();

void changeprompt(const char *newprompt, bool redisplay = false);

const char * getUsageStr(const char *command);

void unescapeifRequired(std::string &what);

void setprompt(prompttype p, std::string arg = "");

prompttype getprompt();

void printHistory();

bool readconfirmationloop(const char *question);

#ifdef _WIN32
void stringtolocalw(const char* path, std::wstring* local);
void localwtostring(const std::wstring* wide, std::string *multibyte);

void utf16ToUtf8(const wchar_t* utf16data, int utf16size, std::string* utf8string);
#endif


#endif // MEGACMDSHELL_H

