#ifndef QTMEGAAPICREATOR_H
#define QTMEGAAPICREATOR_H

#include <megaapi.h>
#include <QList>
#include <QReadWriteLock>

namespace mega
{
class QTMegaApiManager
{
public:
    static void createMegaApi(MegaApi*& api,
                              const char* appKey,
                              const char* basePath,
                              const char* userAgent);

    static void createMegaApi(MegaApi*& api,
                              const char* appKey,
                              MegaGfxProvider* gfxProvider,
                              const char* basePath,
                              const char* userAgent);

    static bool isMegaApiValid(MegaApi* api);
    static void removeMegaApis();

private:
    QTMegaApiManager() = default;

    static QList<MegaApi**> mMegaApis;
    static QReadWriteLock mLock;
};
}

#endif // QTMEGAAPICREATOR_H
