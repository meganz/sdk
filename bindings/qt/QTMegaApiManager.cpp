#include "QTMegaApiManager.h"

#include "megaapi.h"

using namespace mega;

QList<MegaApi**> QTMegaApiManager::mMegaApis = QList<MegaApi**>();
QReadWriteLock QTMegaApiManager::mLock = QReadWriteLock();

void QTMegaApiManager::createMegaApi(MegaApi*& api,
                                     const char* appKey,
                                     const char* basePath,
                                     const char* userAgent,
                                     bool enableKeyPinning)
{
    QWriteLocker lock(&mLock);

    api = new MegaApi(appKey, basePath, userAgent);
    api->setPublicKeyPinning(enableKeyPinning);
    mMegaApis.append(std::addressof(api));
}

void QTMegaApiManager::createMegaApi(MegaApi*& api,
                                     const char* appKey,
                                     MegaGfxProvider* gfxProvider,
                                     const char* basePath,
                                     const char* userAgent,
                                     bool enableKeyPinning)
{
    QWriteLocker lock(&mLock);

    api = new MegaApi(appKey, gfxProvider, basePath, userAgent);
    api->setPublicKeyPinning(enableKeyPinning);
    mMegaApis.append(std::addressof(api));
}

bool QTMegaApiManager::isMegaApiValid(MegaApi* api)
{
    if (api)
    {
        QReadLocker lock(&mLock);
        auto found = std::find_if(mMegaApis.cbegin(),
                                  mMegaApis.cend(),
                                  [api](MegaApi** pToApi)
                                  {
                                      return (*pToApi) == api;
                                  });

        return found != mMegaApis.cend();
    }

    return false;
}

void QTMegaApiManager::removeMegaApis()
{
    QWriteLocker lock(&mLock);

    for (int index = 0; index < mMegaApis.size(); ++index)
    {
        auto& api(*mMegaApis[index]);
        if (api)
        {
            delete api;
            api = nullptr;
        }
    }

    mMegaApis.clear();
}
