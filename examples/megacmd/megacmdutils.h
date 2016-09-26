#ifndef MEGACMDUTILS_H
#define MEGACMDUTILS_H

#include "mega.h"
#include "megaapi.h"

using namespace mega;

/* MegaNode info extracting*/
/**
 * @brief getNumFolderFiles
 *
 * Ownership of returned value belongs to the caller
 * @param n
 * @param api
 * @return
 */
int * getNumFolderFiles(MegaNode *, MegaApi *);

string getUserInSharedNode(MegaNode *n, MegaApi *api);


/* code translation*/
const char* getAttrStr(int attr);

int getAttrNum(const char* attr);

const char* getAccessLevelStr(int level);

const char* getSyncStateStr(int state);

string visibilityToString(int visibility);

const char* errorstring(int e);

const char * getErrorCodeStr(MegaError *e);


/* Files and folders */

bool isFolder(string path);

bool canWrite(string path);

int getLinkType(string link);

bool isPublicLink(string link);

bool isRegularFile(string path);

bool pathExits(string path);

string getCurrentLocalPath();

string expanseLocalPath(string path);

bool hasWildCards(string &what);


/* Time related */

std::string getReadableTime(const time_t rawtime);

time_t getTimeStampAfter(time_t initial, string timestring);

time_t getTimeStampAfter(string timestring);


/* Strings related */

// trim from start
std::string &ltrim(std::string &s, const char &c);

// trim at the end
std::string &rtrim(std::string &s, const char &c);

vector<string> getlistOfWords(char *ptr);

bool stringcontained(const char * s, vector<string> list);

char * dupstr(char* s);

bool patternMatches(const char *what, const char *pattern);


/* Flags and Options */
int getFlag(map<string, int> *flags, const char * optname);

string getOption(map<string, string> *cloptions, const char * optname, string defaultValue = "");

int getintOption(map<string, string> *cloptions, const char * optname, int defaultValue = 0);

bool setOptionsAndFlags(map<string, string> *opts, map<string, int> *flags, vector<string> *ws, set<string> vvalidOptions, bool global = false);

#endif // MEGACMDUTILS_H
