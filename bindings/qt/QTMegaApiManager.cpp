#include "QTMegaApiManager.h"

#include "megaapi.h"

using namespace mega;

QList<MegaApi**> QTMegaApiManager::mMegaApis = QList<MegaApi**>();
QReadWriteLock QTMegaApiManager::mLock = QReadWriteLock();

void QTMegaApiManager::createMegaApi(mega::MegaApi*& api,
                                     const char* appKey,
                                     const char* basePath,
                                     const char* userAgent)
{
    QWriteLocker lock(&mLock);

    api = new mega::MegaApi(appKey, basePath, userAgent);
    mMegaApis.append(std::addressof(api));
}

bool QTMegaApiManager::isMegaApiValid(MegaApi* api)
{
    if (api)
    {
        QReadLocker lock(&mLock);
        auto found = std::find_if(mMegaApis.cbegin(),
                                  mMegaApis.cend(),
                                  [api](mega::MegaApi** pToApi)
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
