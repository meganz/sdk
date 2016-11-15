/**
 * @file examples/megacmd/megacmdutils.h
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

const char * getLogLevelStr(int loglevel);

int getLogLevelNum(const char* level);

const char * getShareLevelStr(int sharelevel);

int getShareLevelNum(const char* level);



/* Files and folders */

bool canWrite(string path);

int getLinkType(string link);

bool isPublicLink(string link);

bool isRegularFile(string path);

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

bool replace(std::string& str, const std::string& from, const std::string& to);

void replaceAll(std::string& str, const std::string& from, const std::string& to);

bool isRegExp(string what);

string unquote(string what);

bool patternMatches(const char *what, const char *pattern);

int toInteger(string what, int failValue = -1);

string joinStrings(const vector<string>& vec, const char* delim = " ");

/* Flags and Options */
int getFlag(map<string, int> *flags, const char * optname);

string getOption(map<string, string> *cloptions, const char * optname, string defaultValue = "");

int getintOption(map<string, string> *cloptions, const char * optname, int defaultValue = 0);

bool setOptionsAndFlags(map<string, string> *opts, map<string, int> *flags, vector<string> *ws, set<string> vvalidOptions, bool global = false);

/* Others */
string sizeToText(long long totalSize, bool equalizeUnitsLength = true, bool humanreadable = true);

#endif // MEGACMDUTILS_H
