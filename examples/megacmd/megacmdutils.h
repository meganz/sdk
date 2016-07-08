#ifndef MEGACMDUTILS_H
#define MEGACMDUTILS_H

#include "mega.h"
#include "megaapi.h"

using namespace mega;

/**
 * @brief getNumFolderFiles
 *
 * Ownership of returned value belongs to the caller
 * @param n
 * @param api
 * @return
 */
int * getNumFolderFiles(MegaNode *,MegaApi *);

const char * getUserInSharedNode(MegaNode *n, MegaApi *api);

const char* getAccessLevelStr(int level);

const char* getSyncStateStr(int state);

const char* errorstring(int e);

const char * getErrorCodeStr(MegaError *e);

bool ifPathAFolder(const char * path);

#endif // MEGACMDUTILS_H
