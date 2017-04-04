#ifndef OSXUTILS_H
#define OSXUTILS_H

#include "mega/proxy.h"

void path2localMac(std::string* path, std::string* local);

#ifdef TARGET_OS_OSX
void getOSXproxy(mega::Proxy* proxy);
#endif

#endif // OSXUTILS_H
