#ifndef OSXUTILS_H
#define OSXUTILS_H

#include "mega/proxy.h"

void path2localMac(const std::string* path, std::string* local);

#if defined(__APPLE__) && !(TARGET_OS_IPHONE)
void getOSXproxy(mega::Proxy* proxy);
#endif

#endif // OSXUTILS_H
