/**
* @file MegaSDK.cpp
* @brief Allows to control a MEGA account or a public folder.
*
* (c) 2013-2014 by Mega Limited, Auckland, New Zealand
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

#include "MegaSDK.h"

using namespace mega;
using namespace Platform;

#define REQUIRED_ENTROPY 64

MegaSDK::~MegaSDK()
{
	delete megaApi;
	DeleteCriticalSection(&listenerMutex);
    DeleteCriticalSection(&loggerMutex);
}

MegaApi *MegaSDK::getCPtr()
{
	return megaApi;
}

MegaSDK::MegaSDK(String^ appKey, String^ userAgent, MRandomNumberProvider ^randomProvider)
{
	//Windows 8.1
	//auto iBuffer = Windows::Security::Cryptography::CryptographicBuffer::GenerateRandom(size);
	//auto reader = Windows::Storage::Streams::DataReader::FromBuffer(iBuffer);
	//reader->ReadBytes(::Platform::ArrayReference<unsigned char>(output, size));

	unsigned char randomData[REQUIRED_ENTROPY];
	if (randomProvider != nullptr)
		randomProvider->GenerateRandomBlock(::Platform::ArrayReference<unsigned char>(randomData, REQUIRED_ENTROPY));
	MegaApi::addEntropy((char *)randomData, REQUIRED_ENTROPY);

	std::string utf8appKey;
	if(appKey != nullptr) 
		MegaApi::utf16ToUtf8(appKey->Data(), appKey->Length(), &utf8appKey);
	
	std::string utf8userAgent;
	if(userAgent != nullptr) 
		MegaApi::utf16ToUtf8(userAgent->Data(), userAgent->Length(), &utf8userAgent);

	megaApi = new MegaApi((appKey != nullptr) ? utf8appKey.c_str() : NULL, 
		(const char *)NULL, (userAgent != nullptr) ? utf8userAgent.c_str() : NULL);
	
    InitializeCriticalSectionEx(&listenerMutex, 0, 0);
    InitializeCriticalSectionEx(&loggerMutex, 0, 0);
}

MegaSDK::MegaSDK(String^ appKey, String^ userAgent, String^ basePath, MRandomNumberProvider ^randomProvider)
{
	//Windows 8.1
	//auto iBuffer = Windows::Security::Cryptography::CryptographicBuffer::GenerateRandom(size);
	//auto reader = Windows::Storage::Streams::DataReader::FromBuffer(iBuffer);
	//reader->ReadBytes(::Platform::ArrayReference<unsigned char>(output, size));

	unsigned char randomData[REQUIRED_ENTROPY];
	if (randomProvider != nullptr)
		randomProvider->GenerateRandomBlock(::Platform::ArrayReference<unsigned char>(randomData, REQUIRED_ENTROPY));
	MegaApi::addEntropy((char *)randomData, REQUIRED_ENTROPY);

	std::string utf8appKey;
	if (appKey != nullptr)
		MegaApi::utf16ToUtf8(appKey->Data(), appKey->Length(), &utf8appKey);

	std::string utf8userAgent;
	if (userAgent != nullptr)
		MegaApi::utf16ToUtf8(userAgent->Data(), userAgent->Length(), &utf8userAgent);

	std::string utf8basePath;
	if (basePath != nullptr)
		MegaApi::utf16ToUtf8(basePath->Data(), basePath->Length(), &utf8basePath);
		
	megaApi = new MegaApi((appKey != nullptr) ? utf8appKey.c_str() : NULL,
		(basePath != nullptr) ? utf8basePath.c_str() : NULL,
		(userAgent != nullptr) ? utf8userAgent.c_str() : NULL);
	
    InitializeCriticalSectionEx(&listenerMutex, 0, 0);
    InitializeCriticalSectionEx(&loggerMutex, 0, 0);
}

MegaSDK::MegaSDK(String^ appKey, String^ userAgent, String^ basePath, MRandomNumberProvider^ randomProvider, MGfxProcessorInterface^ gfxProcessor)
{
	//Windows 8.1
	//auto iBuffer = Windows::Security::Cryptography::CryptographicBuffer::GenerateRandom(size);
	//auto reader = Windows::Storage::Streams::DataReader::FromBuffer(iBuffer);
	//reader->ReadBytes(::Platform::ArrayReference<unsigned char>(output, size));

	unsigned char randomData[REQUIRED_ENTROPY];
	if (randomProvider != nullptr)
		randomProvider->GenerateRandomBlock(::Platform::ArrayReference<unsigned char>(randomData, REQUIRED_ENTROPY));
	MegaApi::addEntropy((char *)randomData, REQUIRED_ENTROPY);

	std::string utf8appKey;
	if (appKey != nullptr)
		MegaApi::utf16ToUtf8(appKey->Data(), appKey->Length(), &utf8appKey);

	std::string utf8userAgent;
	if (userAgent != nullptr)
		MegaApi::utf16ToUtf8(userAgent->Data(), userAgent->Length(), &utf8userAgent);

	std::string utf8basePath;
	if (basePath != nullptr)
		MegaApi::utf16ToUtf8(basePath->Data(), basePath->Length(), &utf8basePath);

	externalGfxProcessor = NULL;
	if (gfxProcessor != nullptr)
		externalGfxProcessor = new DelegateMGfxProcessor(gfxProcessor);

	megaApi = new MegaApi((appKey != nullptr) ? utf8appKey.c_str() : NULL,
		externalGfxProcessor,
		(basePath != nullptr) ? utf8basePath.c_str() : NULL,
		(userAgent != nullptr) ? utf8userAgent.c_str() : NULL);
	
    InitializeCriticalSectionEx(&listenerMutex, 0, 0);
    InitializeCriticalSectionEx(&loggerMutex, 0, 0);
}

void MegaSDK::addListener(MListenerInterface^ listener)
{
	megaApi->addListener(createDelegateMListener(listener));
}

void MegaSDK::addRequestListener(MRequestListenerInterface^ listener)
{
	megaApi->addRequestListener(createDelegateMRequestListener(listener, false));
}

void MegaSDK::addTransferListener(MTransferListenerInterface^ listener)
{
	megaApi->addTransferListener(createDelegateMTransferListener(listener, false));
}

void MegaSDK::addGlobalListener(MGlobalListenerInterface^ listener)
{
	megaApi->addGlobalListener(createDelegateMGlobalListener(listener));
}

void MegaSDK::removeListener(MListenerInterface^ listener)
{
    std::vector<DelegateMListener *> listenersToRemove;

    EnterCriticalSection(&listenerMutex);
    std::set<DelegateMListener *>::iterator it = activeMegaListeners.begin();
    while (it != activeMegaListeners.end())
    {
        DelegateMListener *delegate = *it;
        if (delegate->getUserListener() == listener)
        {
            listenersToRemove.push_back(delegate);
            activeMegaListeners.erase(it++);
        }
        else
        {
            it++;
        }
    }
    LeaveCriticalSection(&listenerMutex);

    for (unsigned int i = 0; i < listenersToRemove.size(); i++)
    {
        megaApi->removeListener(listenersToRemove[i]);
    }
}

void MegaSDK::removeRequestListener(MRequestListenerInterface^ listener)
{
    std::vector<DelegateMRequestListener *> listenersToRemove;

    EnterCriticalSection(&listenerMutex);
    std::set<DelegateMRequestListener *>::iterator it = activeRequestListeners.begin();
    while (it != activeRequestListeners.end())
    {
        DelegateMRequestListener *delegate = *it;
        if (delegate->getUserListener() == listener)
        {
            listenersToRemove.push_back(delegate);
            activeRequestListeners.erase(it++);
        }
        else
        {
            it++;
        }
    }
    LeaveCriticalSection(&listenerMutex);

    for (unsigned int i = 0; i < listenersToRemove.size(); i++)
    {
        megaApi->removeRequestListener(listenersToRemove[i]);
    }
}

void MegaSDK::removeTransferListener(MTransferListenerInterface^ listener)
{
    std::vector<DelegateMTransferListener *> listenersToRemove;

    EnterCriticalSection(&listenerMutex);
    std::set<DelegateMTransferListener *>::iterator it = activeTransferListeners.begin();
    while (it != activeTransferListeners.end())
    {
        DelegateMTransferListener *delegate = *it;
        if (delegate->getUserListener() == listener)
        {
            listenersToRemove.push_back(delegate);
            activeTransferListeners.erase(it++);
        }
        else
        {
            it++;
        }
    }
    LeaveCriticalSection(&listenerMutex);

    for (unsigned int i = 0; i < listenersToRemove.size(); i++)
    {
        megaApi->removeTransferListener(listenersToRemove[i]);
    }
}

void MegaSDK::removeGlobalListener(MGlobalListenerInterface^ listener)
{
    std::vector<DelegateMGlobalListener *> listenersToRemove;

    EnterCriticalSection(&listenerMutex);
    std::set<DelegateMGlobalListener *>::iterator it = activeGlobalListeners.begin();
    while (it != activeGlobalListeners.end())
    {
        DelegateMGlobalListener *delegate = *it;
        if (delegate->getUserListener() == listener)
        {
            listenersToRemove.push_back(delegate);
            activeGlobalListeners.erase(it++);
        }
        else
        {
            it++;
        }
    }
    LeaveCriticalSection(&listenerMutex);

    for (unsigned int i = 0; i < listenersToRemove.size(); i++)
    {
        megaApi->removeGlobalListener(listenersToRemove[i]);
    }
}

String^ MegaSDK::getBase64PwKey(String^ password)
{
	if (password == nullptr) return nullptr;

	std::string utf8password;
	MegaApi::utf16ToUtf8(password->Data(), password->Length(), &utf8password);

	std::string utf16base64PwKey;
	const char *utf8base64PwKey = megaApi->getBase64PwKey(utf8password.c_str());
	if (!utf8base64PwKey)
	{
		return nullptr;
	}

	MegaApi::utf8ToUtf16(utf8base64PwKey, &utf16base64PwKey);
	delete[] utf8base64PwKey;

	return ref new String((wchar_t *)utf16base64PwKey.c_str());
}

String^ MegaSDK::getStringHash(String^ base64pwkey, String^ inBuf)
{
	if (base64pwkey == nullptr || inBuf == nullptr) return nullptr;

	std::string utf8base64pwkey;
	MegaApi::utf16ToUtf8(base64pwkey->Data(), base64pwkey->Length(), &utf8base64pwkey);

	std::string utf8inBuf;
	MegaApi::utf16ToUtf8(inBuf->Data(), inBuf->Length(), &utf8inBuf);

	std::string utf16stringHash;
	const char *utf8stringHash = megaApi->getStringHash(utf8base64pwkey.c_str(), utf8inBuf.c_str());
	if (!utf8stringHash)
	{
		return nullptr;
	}

	MegaApi::utf8ToUtf16(utf8stringHash, &utf16stringHash);
	delete [] utf8stringHash;

	return ref new String((wchar_t *)utf16stringHash.c_str());
}

void MegaSDK::getSessionTransferURL(String^ path, MRequestListenerInterface^ listener)
{
    std::string utf8path;
    if (path != nullptr)
        MegaApi::utf16ToUtf8(path->Data(), path->Length(), &utf8path);

    megaApi->getSessionTransferURL((path != nullptr) ? utf8path.c_str() : NULL,        
        createDelegateMRequestListener(listener));
}

MegaHandle MegaSDK::base32ToHandle(String^ base32Handle)
{
    if (base32Handle == nullptr) return ::mega::INVALID_HANDLE;

    std::string utf8base32Handle;
    MegaApi::utf16ToUtf8(base32Handle->Data(), base32Handle->Length(), &utf8base32Handle);
    return MegaApi::base32ToHandle(utf8base32Handle.c_str());
}

uint64 MegaSDK::base64ToHandle(String^ base64Handle)
{
	if (base64Handle == nullptr) return ::mega::INVALID_HANDLE;

	std::string utf8base64Handle;
	MegaApi::utf16ToUtf8(base64Handle->Data(), base64Handle->Length(), &utf8base64Handle);
	return MegaApi::base64ToHandle(utf8base64Handle.c_str());
}

String^ MegaSDK::handleToBase64(MegaHandle handle)
{
    std::string utf16base64handle;
    const char *utf8base64handle = MegaApi::handleToBase64(handle);
    if (!utf8base64handle)
    {
        return nullptr;
    }

    MegaApi::utf8ToUtf16(utf8base64handle, &utf16base64handle);
    delete[] utf8base64handle;

    return ref new String((wchar_t *)utf16base64handle.data());
}

String^ MegaSDK::userHandleToBase64(MegaHandle handle)
{
    std::string utf16base64userHandle;
    const char *utf8base64userHandle = MegaApi::userHandleToBase64(handle);
    if (!utf8base64userHandle)
    {
        return nullptr;
    }

    MegaApi::utf8ToUtf16(utf8base64userHandle, &utf16base64userHandle);
    delete[] utf8base64userHandle;

    return ref new String((wchar_t *)utf16base64userHandle.data());
}

void MegaSDK::retryPendingConnections(bool disconnect, bool includexfers, MRequestListenerInterface^ listener)
{
    megaApi->retryPendingConnections(disconnect, includexfers,
        createDelegateMRequestListener(listener));
}

void MegaSDK::retryPendingConnections(bool disconnect, bool includexfers)
{
    megaApi->retryPendingConnections(disconnect, includexfers);
}

void MegaSDK::retryPendingConnections(bool disconnect)
{
    megaApi->retryPendingConnections(disconnect);
}

void MegaSDK::retryPendingConnections()
{
	megaApi->retryPendingConnections();
}

void MegaSDK::reconnect()
{
	megaApi->retryPendingConnections(true, true);
}

void MegaSDK::setStatsID(String^ id)
{
    std::string utf8id;
    if (id != nullptr)
        MegaApi::utf16ToUtf8(id->Data(), id->Length(), &utf8id);

    MegaApi::setStatsID((id != nullptr) ? utf8id.c_str() : NULL);
}

void MegaSDK::login(String^ email, String^ password)
{
	std::string utf8email;
	if(email != nullptr) 
		MegaApi::utf16ToUtf8(email->Data(), email->Length(), &utf8email);

	std::string utf8password;
	if(password != nullptr) 
		MegaApi::utf16ToUtf8(password->Data(), password->Length(), &utf8password);

	megaApi->login((email != nullptr) ? utf8email.c_str() : NULL,
		(password != nullptr) ? utf8password.c_str() : NULL);
}

void MegaSDK::login(String^ email, String^ password, MRequestListenerInterface^ listener)
{
	std::string utf8email;
	if (email != nullptr)
		MegaApi::utf16ToUtf8(email->Data(), email->Length(), &utf8email);

	std::string utf8password;
	if (password != nullptr)
		MegaApi::utf16ToUtf8(password->Data(), password->Length(), &utf8password);

	megaApi->login((email != nullptr) ? utf8email.c_str() : NULL,
		(password != nullptr) ? utf8password.c_str() : NULL,
		createDelegateMRequestListener(listener));
}

String^ MegaSDK::getSequenceNumber()
{
    const char *utf8sequenceNumber = megaApi->getSequenceNumber();
    if (!utf8sequenceNumber)
    {
        return nullptr;
    }

    std::string utf16sequenceNumber;
    MegaApi::utf8ToUtf16(utf8sequenceNumber, &utf16sequenceNumber);
    delete[] utf8sequenceNumber;

    return ref new String((wchar_t *)utf16sequenceNumber.c_str());
}

String^ MegaSDK::dumpSession()
{
	const char *utf8session = megaApi->dumpSession();
	if (!utf8session)
	{
		return nullptr;
	}

	std::string utf16session;
	MegaApi::utf8ToUtf16(utf8session, &utf16session);
	delete[] utf8session;

	return ref new String((wchar_t *)utf16session.c_str());
}

String^ MegaSDK::dumpXMPPSession()
{
    const char *utf8session = megaApi->dumpXMPPSession();
    if (!utf8session)
    {
        return nullptr;
    }

    std::string utf16session;
    MegaApi::utf8ToUtf16(utf8session, &utf16session);
    delete[] utf8session;

    return ref new String((wchar_t *)utf16session.c_str());
}

void MegaSDK::fastLogin(String^ email, String^ stringHash, String^ base64pwkey)
{
	std::string utf8email;
	if (email != nullptr)
		MegaApi::utf16ToUtf8(email->Data(), email->Length(), &utf8email);

	std::string utf8stringHash;
	if (stringHash != nullptr)
		MegaApi::utf16ToUtf8(stringHash->Data(), stringHash->Length(), &utf8stringHash);

	std::string utf8base64pwkey;
	if (base64pwkey != nullptr)
		MegaApi::utf16ToUtf8(base64pwkey->Data(), base64pwkey->Length(), &utf8base64pwkey);

	megaApi->fastLogin((email != nullptr) ? utf8email.c_str() : NULL,
		(stringHash != nullptr) ? utf8stringHash.c_str() : NULL,
		(base64pwkey != nullptr) ? utf8base64pwkey.c_str() : NULL);
}

void MegaSDK::fastLogin(String^ email, String^ stringHash, String^ base64pwkey, MRequestListenerInterface^ listener)
{
	std::string utf8email;
	if (email != nullptr)
		MegaApi::utf16ToUtf8(email->Data(), email->Length(), &utf8email);

	std::string utf8stringHash;
	if (stringHash != nullptr)
		MegaApi::utf16ToUtf8(stringHash->Data(), stringHash->Length(), &utf8stringHash);

	std::string utf8base64pwkey;
	if (base64pwkey != nullptr)
		MegaApi::utf16ToUtf8(base64pwkey->Data(), base64pwkey->Length(), &utf8base64pwkey);

	megaApi->fastLogin((email != nullptr) ? utf8email.c_str() : NULL,
		(stringHash != nullptr) ? utf8stringHash.c_str() : NULL,
		(base64pwkey != nullptr) ? utf8base64pwkey.c_str() : NULL,
		createDelegateMRequestListener(listener));
}

void MegaSDK::fastLogin(String^ session)
{
	std::string utf8session;
	if (session != nullptr)
		MegaApi::utf16ToUtf8(session->Data(), session->Length(), &utf8session);
	megaApi->fastLogin((session != nullptr) ? utf8session.c_str() : NULL);
}

void MegaSDK::fastLogin(String^ session, MRequestListenerInterface^ listener)
{
	std::string utf8session;
	if (session != nullptr)
		MegaApi::utf16ToUtf8(session->Data(), session->Length(), &utf8session);
	
	megaApi->fastLogin((session != nullptr) ? utf8session.c_str() : NULL,
		createDelegateMRequestListener(listener));
}

void MegaSDK::killSession(MegaHandle sessionHandle, MRequestListenerInterface^ listener)
{
    megaApi->killSession(sessionHandle, createDelegateMRequestListener(listener));
}

void MegaSDK::killSession(MegaHandle sessionHandle)
{
    megaApi->killSession(sessionHandle);
}

void MegaSDK::killAllSessions(MRequestListenerInterface^ listener)
{
    megaApi->killSession(mega::INVALID_HANDLE, createDelegateMRequestListener(listener));
}

void MegaSDK::killAllSessions()
{
    megaApi->killSession(mega::INVALID_HANDLE);
}

void MegaSDK::getOwnUserData(MRequestListenerInterface^ listener)
{
	megaApi->getUserData(createDelegateMRequestListener(listener));
}

void MegaSDK::getOwnUserData()
{
	megaApi->getUserData();
}

void MegaSDK::getUserData(MUser^ user, MRequestListenerInterface^ listener)
{
	megaApi->getUserData((user != nullptr) ? user->getCPtr() : NULL,
		createDelegateMRequestListener(listener));
}

void MegaSDK::getUserData(MUser^ user)
{
	megaApi->getUserData((user != nullptr) ? user->getCPtr() : NULL);
}

String^ MegaSDK::getAccountAuth()
{
    const char *utf8accountAuth = megaApi->getAccountAuth();
    if (!utf8accountAuth)
    {
        return nullptr;
    }

    std::string utf16accountAuth;
    MegaApi::utf8ToUtf16(utf8accountAuth, &utf16accountAuth);
    delete[] utf8accountAuth;

    return ref new String((wchar_t *)utf16accountAuth.c_str());
}

void MegaSDK::setAccountAuth(String^ auth)
{
    std::string utf8auth;
    if (auth != nullptr)
        MegaApi::utf16ToUtf8(auth->Data(), auth->Length(), &utf8auth);

    megaApi->setAccountAuth((auth != nullptr) ? utf8auth.c_str() : NULL);
}

void MegaSDK::getUserDataById(String^ user, MRequestListenerInterface^ listener)
{
	std::string utf8user;
	if (user != nullptr)
		MegaApi::utf16ToUtf8(user->Data(), user->Length(), &utf8user);

	megaApi->getUserData((user != nullptr) ? utf8user.c_str() : NULL,
		createDelegateMRequestListener(listener));
}

void MegaSDK::getUserDataById(String^ user)
{
	std::string utf8user;
	if (user != nullptr)
		MegaApi::utf16ToUtf8(user->Data(), user->Length(), &utf8user);

	megaApi->getUserData((user != nullptr) ? utf8user.c_str() : NULL);
}

void MegaSDK::createAccount(String^ email, String^ password, String^ firstname, String^ lastname)
{
    std::string utf8email;
    if (email != nullptr)
        MegaApi::utf16ToUtf8(email->Data(), email->Length(), &utf8email);

    std::string utf8password;
    if (password != nullptr)
        MegaApi::utf16ToUtf8(password->Data(), password->Length(), &utf8password);

    std::string utf8firstname;
    if (firstname != nullptr)
        MegaApi::utf16ToUtf8(firstname->Data(), firstname->Length(), &utf8firstname);

    std::string utf8lastname;
    if (lastname != nullptr)
        MegaApi::utf16ToUtf8(lastname->Data(), lastname->Length(), &utf8lastname);

    megaApi->createAccount((email != nullptr) ? utf8email.c_str() : NULL,
        (password != nullptr) ? utf8password.c_str() : NULL,
        (firstname != nullptr) ? utf8firstname.c_str() : NULL,
        (lastname != nullptr) ? utf8lastname.c_str() : NULL);
}

void MegaSDK::createAccount(String^ email, String^ password, String^ firstname, String^ lastname, MRequestListenerInterface^ listener)
{
    std::string utf8email;
    if (email != nullptr)
        MegaApi::utf16ToUtf8(email->Data(), email->Length(), &utf8email);

    std::string utf8password;
    if (password != nullptr)
        MegaApi::utf16ToUtf8(password->Data(), password->Length(), &utf8password);

    std::string utf8firstname;
    if (firstname != nullptr)
        MegaApi::utf16ToUtf8(firstname->Data(), firstname->Length(), &utf8firstname);

    std::string utf8lastname;
    if (lastname != nullptr)
        MegaApi::utf16ToUtf8(lastname->Data(), lastname->Length(), &utf8lastname);

    megaApi->createAccount((email != nullptr) ? utf8email.c_str() : NULL,
        (password != nullptr) ? utf8password.c_str() : NULL,
        (firstname != nullptr) ? utf8firstname.c_str() : NULL,
        (lastname != nullptr) ? utf8lastname.c_str() : NULL,
        createDelegateMRequestListener(listener));
}

void MegaSDK::fastCreateAccount(String^ email, String^ base64pwkey, String^ name)
{
	std::string utf8email;
	if (email != nullptr)
		MegaApi::utf16ToUtf8(email->Data(), email->Length(), &utf8email);

	std::string utf8base64pwkey;
	if (base64pwkey != nullptr)
		MegaApi::utf16ToUtf8(base64pwkey->Data(), base64pwkey->Length(), &utf8base64pwkey);

	std::string utf8name;
	if (name != nullptr)
		MegaApi::utf16ToUtf8(name->Data(), name->Length(), &utf8name);

	megaApi->fastCreateAccount((email != nullptr) ? utf8email.c_str() : NULL,
		(base64pwkey != nullptr) ? utf8base64pwkey.c_str() : NULL,
		(name != nullptr) ? utf8name.c_str() : NULL);
}

void MegaSDK::fastCreateAccount(String^ email, String^ base64pwkey, String^ name, MRequestListenerInterface^ listener)
{
	std::string utf8email;
	if (email != nullptr)
		MegaApi::utf16ToUtf8(email->Data(), email->Length(), &utf8email);

	std::string utf8base64pwkey;
	if (base64pwkey != nullptr)
		MegaApi::utf16ToUtf8(base64pwkey->Data(), base64pwkey->Length(), &utf8base64pwkey);

	std::string utf8name;
	if (name != nullptr)
		MegaApi::utf16ToUtf8(name->Data(), name->Length(), &utf8name);

	megaApi->fastCreateAccount((email != nullptr) ? utf8email.c_str() : NULL,
		(base64pwkey != nullptr) ? utf8base64pwkey.c_str() : NULL,
		(name != nullptr) ? utf8name.c_str() : NULL,
		createDelegateMRequestListener(listener));
}

void MegaSDK::resumeCreateAccount(String^ sid, MRequestListenerInterface^ listener)
{
    std::string utf8sid;
    if (sid != nullptr)
        MegaApi::utf16ToUtf8(sid->Data(), sid->Length(), &utf8sid);

    megaApi->resumeCreateAccount((sid != nullptr) ? utf8sid.c_str() : NULL,
        createDelegateMRequestListener(listener));
}

void MegaSDK::resumeCreateAccount(String^ sid)
{
    std::string utf8sid;
    if (sid != nullptr)
        MegaApi::utf16ToUtf8(sid->Data(), sid->Length(), &utf8sid);

    megaApi->resumeCreateAccount((sid != nullptr) ? utf8sid.c_str() : NULL);
}

void MegaSDK::sendSignupLink(String^ email, String^ name, String^ password, MRequestListenerInterface^ listener)
{
    std::string utf8email;
    if (email != nullptr)
        MegaApi::utf16ToUtf8(email->Data(), email->Length(), &utf8email);

    std::string utf8name;
    if (name != nullptr)
        MegaApi::utf16ToUtf8(name->Data(), name->Length(), &utf8name);

    std::string utf8password;
    if (password != nullptr)
        MegaApi::utf16ToUtf8(password->Data(), password->Length(), &utf8password);

    megaApi->sendSignupLink((email != nullptr) ? utf8email.c_str() : NULL,
        (name != nullptr) ? utf8name.c_str() : NULL,
        (password != nullptr) ? utf8password.c_str() : NULL,
        createDelegateMRequestListener(listener));
}

void MegaSDK::sendSignupLink(String^ email, String^ name, String^ password)
{
    std::string utf8email;
    if (email != nullptr)
        MegaApi::utf16ToUtf8(email->Data(), email->Length(), &utf8email);

    std::string utf8name;
    if (name != nullptr)
        MegaApi::utf16ToUtf8(name->Data(), name->Length(), &utf8name);

    std::string utf8password;
    if (password != nullptr)
        MegaApi::utf16ToUtf8(password->Data(), password->Length(), &utf8password);

    megaApi->sendSignupLink((email != nullptr) ? utf8email.c_str() : NULL,
        (name != nullptr) ? utf8name.c_str() : NULL,
        (password != nullptr) ? utf8password.c_str() : NULL);
}

void MegaSDK::fastSendSignupLink(String^ email, String^ base64pwkey, String^ name, MRequestListenerInterface^ listener)
{
    std::string utf8email;
    if (email != nullptr)
        MegaApi::utf16ToUtf8(email->Data(), email->Length(), &utf8email);

    std::string utf8base64pwkey;
    if (base64pwkey != nullptr)
        MegaApi::utf16ToUtf8(base64pwkey->Data(), base64pwkey->Length(), &utf8base64pwkey);

    std::string utf8name;
    if (name != nullptr)
        MegaApi::utf16ToUtf8(name->Data(), name->Length(), &utf8name);

    megaApi->fastSendSignupLink((email != nullptr) ? utf8email.c_str() : NULL,
        (base64pwkey != nullptr) ? utf8base64pwkey.c_str() : NULL,
        (name != nullptr) ? utf8name.c_str() : NULL,
        createDelegateMRequestListener(listener));
}

void MegaSDK::fastSendSignupLink(String^ email, String^ base64pwkey, String^ name)
{
    std::string utf8email;
    if (email != nullptr)
        MegaApi::utf16ToUtf8(email->Data(), email->Length(), &utf8email);

    std::string utf8base64pwkey;
    if (base64pwkey != nullptr)
        MegaApi::utf16ToUtf8(base64pwkey->Data(), base64pwkey->Length(), &utf8base64pwkey);

    std::string utf8name;
    if (name != nullptr)
        MegaApi::utf16ToUtf8(name->Data(), name->Length(), &utf8name);

    megaApi->fastSendSignupLink((email != nullptr) ? utf8email.c_str() : NULL,
        (base64pwkey != nullptr) ? utf8base64pwkey.c_str() : NULL,
        (name != nullptr) ? utf8name.c_str() : NULL);
}

void MegaSDK::querySignupLink(String^ link)
{
	std::string utf8link;
	if (link != nullptr)
		MegaApi::utf16ToUtf8(link->Data(), link->Length(), &utf8link);

	megaApi->querySignupLink((link != nullptr) ? utf8link.c_str() : NULL);
}

void MegaSDK::querySignupLink(String^ link, MRequestListenerInterface^ listener)
{
	std::string utf8link;
	if (link != nullptr)
		MegaApi::utf16ToUtf8(link->Data(), link->Length(), &utf8link);

	megaApi->querySignupLink((link != nullptr) ? utf8link.c_str() : NULL,
		createDelegateMRequestListener(listener));
}

void MegaSDK::confirmAccount(String^ link, String^ password)
{
	std::string utf8link;
	if (link != nullptr)
		MegaApi::utf16ToUtf8(link->Data(), link->Length(), &utf8link);

	std::string utf8password;
	if (password != nullptr)
		MegaApi::utf16ToUtf8(password->Data(), password->Length(), &utf8password);

	megaApi->confirmAccount((link != nullptr) ? utf8link.c_str() : NULL,
		(password != nullptr) ? utf8password.c_str() : NULL);
}

void MegaSDK::confirmAccount(String^ link, String^ password, MRequestListenerInterface^ listener)
{
	std::string utf8link;
	if (link != nullptr)
		MegaApi::utf16ToUtf8(link->Data(), link->Length(), &utf8link);

	std::string utf8password;
	if (password != nullptr)
		MegaApi::utf16ToUtf8(password->Data(), password->Length(), &utf8password);

	megaApi->confirmAccount((link != nullptr) ? utf8link.c_str() : NULL,
		(password != nullptr) ? utf8password.c_str() : NULL,
		createDelegateMRequestListener(listener));
}

void MegaSDK::fastConfirmAccount(String^ link, String^ base64pwkey)
{
	std::string utf8link;
	if (link != nullptr)
		MegaApi::utf16ToUtf8(link->Data(), link->Length(), &utf8link);

	std::string utf8base64pwkey;
	if (base64pwkey != nullptr)
		MegaApi::utf16ToUtf8(base64pwkey->Data(), base64pwkey->Length(), &utf8base64pwkey);

	megaApi->fastConfirmAccount((link != nullptr) ? utf8link.c_str() : NULL,
		(base64pwkey != nullptr) ? utf8base64pwkey.c_str() : NULL);
}

void MegaSDK::fastConfirmAccount(String^ link, String^ base64pwkey, MRequestListenerInterface^ listener)
{
	std::string utf8link;
	if (link != nullptr)
		MegaApi::utf16ToUtf8(link->Data(), link->Length(), &utf8link);

	std::string utf8base64pwkey;
	if (base64pwkey != nullptr)
		MegaApi::utf16ToUtf8(base64pwkey->Data(), base64pwkey->Length(), &utf8base64pwkey);

	megaApi->fastConfirmAccount((link != nullptr) ? utf8link.c_str() : NULL,
		(base64pwkey != nullptr) ? utf8base64pwkey.c_str() : NULL,
		createDelegateMRequestListener(listener));
}

void MegaSDK::resetPassword(String^ email, bool hasMasterKey, MRequestListenerInterface^ listener)
{
    std::string utf8email;
    if (email != nullptr)
        MegaApi::utf16ToUtf8(email->Data(), email->Length(), &utf8email);

    megaApi->resetPassword((email != nullptr) ? utf8email.c_str() : NULL,
        hasMasterKey, createDelegateMRequestListener(listener));
}

void MegaSDK::resetPassword(String^ email, bool hasMasterKey)
{
    std::string utf8email;
    if (email != nullptr)
        MegaApi::utf16ToUtf8(email->Data(), email->Length(), &utf8email);

    megaApi->resetPassword((email != nullptr) ? utf8email.c_str() : NULL, hasMasterKey);
}

void MegaSDK::queryResetPasswordLink(String^ link, MRequestListenerInterface^ listener)
{
    std::string utf8link;
    if (link != nullptr)
        MegaApi::utf16ToUtf8(link->Data(), link->Length(), &utf8link);

    megaApi->queryResetPasswordLink((link != nullptr) ? utf8link.c_str() : NULL,
        createDelegateMRequestListener(listener));
}

void MegaSDK::queryResetPasswordLink(String^ link)
{
    std::string utf8link;
    if (link != nullptr)
        MegaApi::utf16ToUtf8(link->Data(), link->Length(), &utf8link);

    megaApi->queryResetPasswordLink((link != nullptr) ? utf8link.c_str() : NULL);
}

void MegaSDK::confirmResetPassword(String^ link, String^ newPwd, String^ masterKey, MRequestListenerInterface^ listener)
{
    std::string utf8link;
    if (link != nullptr)
        MegaApi::utf16ToUtf8(link->Data(), link->Length(), &utf8link);

    std::string utf8newPwd;
    if (newPwd != nullptr)
        MegaApi::utf16ToUtf8(newPwd->Data(), newPwd->Length(), &utf8newPwd);

    std::string utf8masterKey;
    if (masterKey != nullptr)
        MegaApi::utf16ToUtf8(masterKey->Data(), masterKey->Length(), &utf8masterKey);

    megaApi->confirmResetPassword((link != nullptr) ? utf8link.c_str() : NULL,
        (newPwd != nullptr) ? utf8newPwd.c_str() : NULL,
        (masterKey != nullptr) ? utf8masterKey.c_str() : NULL,
        createDelegateMRequestListener(listener));
}

void MegaSDK::confirmResetPassword(String^ link, String^ newPwd, String^ masterKey)
{
    std::string utf8link;
    if (link != nullptr)
        MegaApi::utf16ToUtf8(link->Data(), link->Length(), &utf8link);

    std::string utf8newPwd;
    if (newPwd != nullptr)
        MegaApi::utf16ToUtf8(newPwd->Data(), newPwd->Length(), &utf8newPwd);

    std::string utf8masterKey;
    if (masterKey != nullptr)
        MegaApi::utf16ToUtf8(masterKey->Data(), masterKey->Length(), &utf8masterKey);

    megaApi->confirmResetPassword((link != nullptr) ? utf8link.c_str() : NULL,
        (newPwd != nullptr) ? utf8newPwd.c_str() : NULL,
        (masterKey != nullptr) ? utf8masterKey.c_str() : NULL);
}

void MegaSDK::confirmResetPasswordWithoutMasterKey(String^ link, String^ newPwd, MRequestListenerInterface^ listener)
{
    std::string utf8link;
    if (link != nullptr)
        MegaApi::utf16ToUtf8(link->Data(), link->Length(), &utf8link);

    std::string utf8newPwd;
    if (newPwd != nullptr)
        MegaApi::utf16ToUtf8(newPwd->Data(), newPwd->Length(), &utf8newPwd);
    
    megaApi->confirmResetPassword((link != nullptr) ? utf8link.c_str() : NULL,
        (newPwd != nullptr) ? utf8newPwd.c_str() : NULL, NULL,        
        createDelegateMRequestListener(listener));
}

void MegaSDK::confirmResetPasswordWithoutMasterKey(String^ link, String^ newPwd)
{
    std::string utf8link;
    if (link != nullptr)
        MegaApi::utf16ToUtf8(link->Data(), link->Length(), &utf8link);

    std::string utf8newPwd;
    if (newPwd != nullptr)
        MegaApi::utf16ToUtf8(newPwd->Data(), newPwd->Length(), &utf8newPwd);

    megaApi->confirmResetPassword((link != nullptr) ? utf8link.c_str() : NULL,
        (newPwd != nullptr) ? utf8newPwd.c_str() : NULL);
}

void MegaSDK::cancelAccount(MRequestListenerInterface^ listener)
{
    megaApi->cancelAccount(createDelegateMRequestListener(listener));
}

void MegaSDK::cancelAccount()
{
    megaApi->cancelAccount();
}

void MegaSDK::queryCancelLink(String^ link, MRequestListenerInterface^ listener)
{
    std::string utf8link;
    if (link != nullptr)
        MegaApi::utf16ToUtf8(link->Data(), link->Length(), &utf8link);

    megaApi->queryCancelLink((link != nullptr) ? utf8link.c_str() : NULL,
        createDelegateMRequestListener(listener));
}

void MegaSDK::queryCancelLink(String^ link)
{
    std::string utf8link;
    if (link != nullptr)
        MegaApi::utf16ToUtf8(link->Data(), link->Length(), &utf8link);

    megaApi->queryCancelLink((link != nullptr) ? utf8link.c_str() : NULL);
}

void MegaSDK::confirmCancelAccount(String^ link, String^ pwd, MRequestListenerInterface^ listener)
{
    std::string utf8link;
    if (link != nullptr)
        MegaApi::utf16ToUtf8(link->Data(), link->Length(), &utf8link);

    std::string utf8pwd;
    if (pwd != nullptr)
        MegaApi::utf16ToUtf8(pwd->Data(), pwd->Length(), &utf8pwd);

    megaApi->confirmCancelAccount((link != nullptr) ? utf8link.c_str() : NULL,
        (pwd != nullptr) ? utf8pwd.c_str() : NULL,
        createDelegateMRequestListener(listener));
}

void MegaSDK::confirmCancelAccount(String^ link, String^ pwd)
{
    std::string utf8link;
    if (link != nullptr)
        MegaApi::utf16ToUtf8(link->Data(), link->Length(), &utf8link);

    std::string utf8pwd;
    if (pwd != nullptr)
        MegaApi::utf16ToUtf8(pwd->Data(), pwd->Length(), &utf8pwd);

    megaApi->confirmCancelAccount((link != nullptr) ? utf8link.c_str() : NULL,
        (pwd != nullptr) ? utf8pwd.c_str() : NULL);
}

void MegaSDK::changeEmail(String^ email, MRequestListenerInterface^ listener)
{
    std::string utf8email;
    if (email != nullptr)
        MegaApi::utf16ToUtf8(email->Data(), email->Length(), &utf8email);

    megaApi->changeEmail((email != nullptr) ? utf8email.c_str() : NULL,
        createDelegateMRequestListener(listener));
}

void MegaSDK::changeEmail(String^ email)
{
    std::string utf8email;
    if (email != nullptr)
        MegaApi::utf16ToUtf8(email->Data(), email->Length(), &utf8email);

    megaApi->changeEmail((email != nullptr) ? utf8email.c_str() : NULL);
}

void MegaSDK::queryChangeEmailLink(String^ link, MRequestListenerInterface^ listener)
{
    std::string utf8link;
    if (link != nullptr)
        MegaApi::utf16ToUtf8(link->Data(), link->Length(), &utf8link);

    megaApi->queryChangeEmailLink((link != nullptr) ? utf8link.c_str() : NULL,
        createDelegateMRequestListener(listener));
}

void MegaSDK::queryChangeEmailLink(String^ link)
{
    std::string utf8link;
    if (link != nullptr)
        MegaApi::utf16ToUtf8(link->Data(), link->Length(), &utf8link);

    megaApi->queryChangeEmailLink((link != nullptr) ? utf8link.c_str() : NULL);
}


void MegaSDK::confirmChangeEmail(String^ link, String^ pwd, MRequestListenerInterface^ listener)
{
    std::string utf8link;
    if (link != nullptr)
        MegaApi::utf16ToUtf8(link->Data(), link->Length(), &utf8link);

    std::string utf8pwd;
    if (pwd != nullptr)
        MegaApi::utf16ToUtf8(pwd->Data(), pwd->Length(), &utf8pwd);

    megaApi->confirmChangeEmail((link != nullptr) ? utf8link.c_str() : NULL,
        (pwd != nullptr) ? utf8pwd.c_str() : NULL,
        createDelegateMRequestListener(listener));
}

void MegaSDK::confirmChangeEmail(String^ link, String^ pwd)
{
    std::string utf8link;
    if (link != nullptr)
        MegaApi::utf16ToUtf8(link->Data(), link->Length(), &utf8link);

    std::string utf8pwd;
    if (pwd != nullptr)
        MegaApi::utf16ToUtf8(pwd->Data(), pwd->Length(), &utf8pwd);

    megaApi->confirmChangeEmail((link != nullptr) ? utf8link.c_str() : NULL,
        (pwd != nullptr) ? utf8pwd.c_str() : NULL);
}

int MegaSDK::isLoggedIn()
{
	return megaApi->isLoggedIn();
}

String^ MegaSDK::getMyEmail()
{
	std::string utf16email;
	const char *utf8email = megaApi->getMyEmail();
	if (!utf8email)
	{
		return nullptr;
	}

	MegaApi::utf8ToUtf16(utf8email, &utf16email);
	delete[] utf8email;

	return ref new String((wchar_t *)utf16email.c_str());
}

String^ MegaSDK::getMyUserHandle()
{
    std::string utf16userHandle;
    const char *utf8userHandle = megaApi->getMyUserHandle();

    if (!utf8userHandle)
    {
        return nullptr;
    }

    MegaApi::utf8ToUtf16(utf8userHandle, &utf16userHandle);
    delete[] utf8userHandle;

    return ref new String((wchar_t *)utf16userHandle.c_str());
}

MegaHandle MegaSDK::getMyUserHandleBinary()
{
    return megaApi->getMyUserHandleBinary();
}

MUser^ MegaSDK::getMyUser()
{
    return ref new MUser(megaApi->getMyUser(), true);
}

bool MegaSDK::isAchievementsEnabled()
{
    return megaApi->isAchievementsEnabled();
}

void MegaSDK::setLogLevel(MLogLevel logLevel)
{
    MegaApi::setLogLevel((int)logLevel);
}

void MegaSDK::addLoggerObject(MLoggerInterface^ logger)
{
    MegaApi::addLoggerObject(createDelegateMLogger(logger));
}

void MegaSDK::removeLoggerObject(MLoggerInterface^ logger)
{
    std::vector<DelegateMLogger *> loggersToRemove;

    EnterCriticalSection(&loggerMutex);
    std::set<DelegateMLogger *>::iterator it = activeLoggers.begin();
    while (it != activeLoggers.end())
    {
        DelegateMLogger *delegate = *it;
        if (delegate->getUserLogger() == logger)
        {
            loggersToRemove.push_back(delegate);
            activeLoggers.erase(it++);
        }
        else
        {
            it++;
        }
    }
    LeaveCriticalSection(&loggerMutex);

    for (unsigned int i = 0; i < loggersToRemove.size(); i++)
    {
        MegaApi::removeLoggerObject(loggersToRemove[i]);
        freeLogger(loggersToRemove[i]);
    }
}

void MegaSDK::log(MLogLevel logLevel, String^ message, String^ filename, int line)
{
    std::string utf8message;
    if (message != nullptr)
        MegaApi::utf16ToUtf8(message->Data(), message->Length(), &utf8message);

    std::string utf8filename;
    if (message != nullptr)
        MegaApi::utf16ToUtf8(filename->Data(), filename->Length(), &utf8filename);

    MegaApi::log((int)logLevel, (message != nullptr) ? utf8message.c_str() : NULL, (filename != nullptr) ? utf8filename.c_str() : NULL, line);
}

void MegaSDK::log(MLogLevel logLevel, String^ message, String^ filename)
{
    std::string utf8message;
    if (message != nullptr)
        MegaApi::utf16ToUtf8(message->Data(), message->Length(), &utf8message);

    std::string utf8filename;
    if (message != nullptr)
        MegaApi::utf16ToUtf8(filename->Data(), filename->Length(), &utf8filename);

    MegaApi::log((int)logLevel, (message != nullptr) ? utf8message.c_str() : NULL, (filename != nullptr) ? utf8filename.c_str() : NULL);
}

void MegaSDK::log(MLogLevel logLevel, String^ message)
{
    std::string utf8message;
    if (message != nullptr)
        MegaApi::utf16ToUtf8(message->Data(), message->Length(), &utf8message);

    MegaApi::log((int)logLevel, (message != nullptr) ? utf8message.c_str() : NULL);
}

void MegaSDK::createFolder(String^ name, MNode^ parent, MRequestListenerInterface^ listener)
{
	std::string utf8name;
	if (name != nullptr)
		MegaApi::utf16ToUtf8(name->Data(), name->Length(), &utf8name);

	megaApi->createFolder((name != nullptr) ? utf8name.c_str() : NULL,
		(parent != nullptr) ? parent->getCPtr() : NULL, 
		createDelegateMRequestListener(listener));
}

void MegaSDK::createFolder(String^ name, MNode^ parent)
{
	std::string utf8name;
	if (name != nullptr)
		MegaApi::utf16ToUtf8(name->Data(), name->Length(), &utf8name);

	megaApi->createFolder((name != nullptr) ? utf8name.c_str() : NULL,
		(parent != nullptr) ? parent->getCPtr() : NULL);
}

bool MegaSDK::createLocalFolder(String^ localPath)
{
    std::string utf8localPath;
    if (localPath != nullptr)
        MegaApi::utf16ToUtf8(localPath->Data(), localPath->Length(), &utf8localPath);

    return megaApi->createLocalFolder((localPath != nullptr) ? utf8localPath.c_str() : NULL);
}

void MegaSDK::moveNode(MNode^ node, MNode^ newParent, MRequestListenerInterface^ listener)
{
	megaApi->moveNode((node != nullptr) ? node->getCPtr() : NULL, 
		(newParent != nullptr) ? newParent->getCPtr() : NULL, 
		createDelegateMRequestListener(listener));
}

void MegaSDK::moveNode(MNode^ node, MNode^ newParent)
{
	megaApi->moveNode((node != nullptr) ? node->getCPtr() : NULL, 
		(newParent != nullptr) ? newParent->getCPtr() : NULL);
}

void MegaSDK::copyNode(MNode^ node, MNode^ newParent, MRequestListenerInterface^ listener)
{
	megaApi->copyNode((node != nullptr) ? node->getCPtr() : NULL, 
		(newParent != nullptr) ? newParent->getCPtr() : NULL,
		createDelegateMRequestListener(listener));
}

void MegaSDK::copyNode(MNode^ node, MNode^ newParent)
{
	megaApi->copyNode((node != nullptr) ? node->getCPtr() : NULL, 
		(newParent != nullptr) ? newParent->getCPtr() : NULL);
}

void MegaSDK::copyAndRenameNode(MNode^ node, MNode^ newParent, String^ newName, MRequestListenerInterface^ listener)
{
    std::string utf8newName;
    if (newName != nullptr)
        MegaApi::utf16ToUtf8(newName->Data(), newName->Length(), &utf8newName);

    megaApi->copyNode((node != nullptr) ? node->getCPtr() : NULL,
        (newParent != nullptr) ? newParent->getCPtr() : NULL,
        (newName != nullptr) ? utf8newName.c_str() : NULL,
        createDelegateMRequestListener(listener));
}

void MegaSDK::copyAndRenameNode(MNode^ node, MNode^ newParent, String^ newName)
{
    std::string utf8newName;
    if (newName != nullptr)
        MegaApi::utf16ToUtf8(newName->Data(), newName->Length(), &utf8newName);

    megaApi->copyNode((node != nullptr) ? node->getCPtr() : NULL,
        (newParent != nullptr) ? newParent->getCPtr() : NULL,
        (newName != nullptr) ? utf8newName.c_str() : NULL);
}

void MegaSDK::renameNode(MNode^ node, String^ newName, MRequestListenerInterface^ listener)
{
	std::string utf8newName;
	if (newName != nullptr)
		MegaApi::utf16ToUtf8(newName->Data(), newName->Length(), &utf8newName);

	megaApi->renameNode((node != nullptr) ? node->getCPtr() : NULL, 
		(newName != nullptr) ? utf8newName.c_str() : NULL,
		createDelegateMRequestListener(listener));
}

void MegaSDK::renameNode(MNode^ node, String^ newName)
{
	std::string utf8newName;
	if (newName != nullptr)
		MegaApi::utf16ToUtf8(newName->Data(), newName->Length(), &utf8newName);

	megaApi->renameNode((node != nullptr) ? node->getCPtr() : NULL, 
		(newName != nullptr) ? utf8newName.c_str() : NULL);
}

void MegaSDK::remove(MNode^ node, MRequestListenerInterface^ listener)
{
	megaApi->remove((node != nullptr) ? node->getCPtr() : NULL, 
		createDelegateMRequestListener(listener));
}

void MegaSDK::remove(MNode^ node)
{
	megaApi->remove((node != nullptr) ? node->getCPtr() : NULL);
}

void MegaSDK::cleanRubbishBin(MRequestListenerInterface^ listener)
{
    megaApi->cleanRubbishBin(createDelegateMRequestListener(listener));
}

void MegaSDK::cleanRubbishBin()
{
    megaApi->cleanRubbishBin();
}

void MegaSDK::sendFileToUser(MNode^ node, MUser^ user, MRequestListenerInterface^ listener)
{
    megaApi->sendFileToUser((node != nullptr) ? node->getCPtr() : NULL,
        (user != nullptr) ? user->getCPtr() : NULL,
        createDelegateMRequestListener(listener));
}

void MegaSDK::sendFileToUser(MNode^ node, MUser^ user)
{
    megaApi->sendFileToUser((node != nullptr) ? node->getCPtr() : NULL,
        (user != nullptr) ? user->getCPtr() : NULL);
}

void MegaSDK::sendFileToUserByEmail(MNode^ node, String^ email, MRequestListenerInterface^ listener)
{
    std::string utf8email;
    if (email != nullptr)
        MegaApi::utf16ToUtf8(email->Data(), email->Length(), &utf8email);

    megaApi->sendFileToUser((node != nullptr) ? node->getCPtr() : NULL,
        (email != nullptr) ? utf8email.c_str() : NULL,
        createDelegateMRequestListener(listener));
}

void MegaSDK::sendFileToUserByEmail(MNode^ node, String^ email)
{
    std::string utf8email;
    if (email != nullptr)
        MegaApi::utf16ToUtf8(email->Data(), email->Length(), &utf8email);

    megaApi->sendFileToUser((node != nullptr) ? node->getCPtr() : NULL,
        (email != nullptr) ? utf8email.c_str() : NULL);
}

void MegaSDK::share(MNode^ node, MUser^ user, int level, MRequestListenerInterface^ listener)
{
	megaApi->share((node != nullptr) ? node->getCPtr() : NULL, 
		(user != nullptr) ? user->getCPtr() : NULL, level, 
		createDelegateMRequestListener(listener));
}

void MegaSDK::share(MNode^ node, MUser^ user, int level)
{
	megaApi->share((node != nullptr) ? node->getCPtr() : NULL, 
		(user != nullptr) ? user->getCPtr() : NULL, level);
}

void MegaSDK::shareByEmail(MNode^ node, String^ email, int level, MRequestListenerInterface^ listener)
{
    std::string utf8email;
    if (email != nullptr)
        MegaApi::utf16ToUtf8(email->Data(), email->Length(), &utf8email);

    megaApi->share((node != nullptr) ? node->getCPtr() : NULL,
        (email != nullptr) ? utf8email.c_str() : NULL, level,
        createDelegateMRequestListener(listener));
}

void MegaSDK::shareByEmail(MNode^ node, String^ email, int level)
{
    std::string utf8email;
    if (email != nullptr)
        MegaApi::utf16ToUtf8(email->Data(), email->Length(), &utf8email);

    megaApi->share((node != nullptr) ? node->getCPtr() : NULL,
        (email != nullptr) ? utf8email.c_str() : NULL, level);
}

void MegaSDK::loginToFolder(String^ megaFolderLink, MRequestListenerInterface^ listener)
{
	std::string utf8megaFolderLink;
	if (megaFolderLink != nullptr)
		MegaApi::utf16ToUtf8(megaFolderLink->Data(), megaFolderLink->Length(), &utf8megaFolderLink);

	megaApi->loginToFolder((megaFolderLink != nullptr) ? utf8megaFolderLink.c_str() : NULL,
		createDelegateMRequestListener(listener));
}

void MegaSDK::loginToFolder(String^ megaFolderLink)
{
	std::string utf8megaFolderLink;
	if (megaFolderLink != nullptr)
		MegaApi::utf16ToUtf8(megaFolderLink->Data(), megaFolderLink->Length(), &utf8megaFolderLink);

	megaApi->loginToFolder((megaFolderLink != nullptr) ? utf8megaFolderLink.c_str() : nullptr);
}

void MegaSDK::importFileLink(String^ megaFileLink, MNode^ parent, MRequestListenerInterface^ listener)
{
	std::string utf8megaFileLink;
	if (megaFileLink != nullptr)
		MegaApi::utf16ToUtf8(megaFileLink->Data(), megaFileLink->Length(), &utf8megaFileLink);

	megaApi->importFileLink((megaFileLink != nullptr) ? utf8megaFileLink.c_str() : NULL,
		(parent != nullptr) ? parent->getCPtr() : NULL, 
		createDelegateMRequestListener(listener));
}

void MegaSDK::importFileLink(String^ megaFileLink, MNode^ parent)
{
	std::string utf8megaFileLink;
	if (megaFileLink != nullptr)
		MegaApi::utf16ToUtf8(megaFileLink->Data(), megaFileLink->Length(), &utf8megaFileLink);

	megaApi->importFileLink((megaFileLink != nullptr) ? utf8megaFileLink.c_str() : NULL,
		(parent != nullptr) ? parent->getCPtr() : NULL);
}

void MegaSDK::decryptPasswordProtectedLink(String^ link, String^ password, MRequestListenerInterface^ listener)
{
    std::string utf8link;
    if (link != nullptr)
        MegaApi::utf16ToUtf8(link->Data(), link->Length(), &utf8link);

    std::string utf8password;
    if (password != nullptr)
        MegaApi::utf16ToUtf8(password->Data(), password->Length(), &utf8password);

    megaApi->decryptPasswordProtectedLink((link != nullptr) ? utf8link.c_str() : NULL,
        (password != nullptr) ? utf8password.c_str() : NULL,
        createDelegateMRequestListener(listener));
}

void MegaSDK::decryptPasswordProtectedLink(String^ link, String^ password)
{
    std::string utf8link;
    if (link != nullptr)
        MegaApi::utf16ToUtf8(link->Data(), link->Length(), &utf8link);

    std::string utf8password;
    if (password != nullptr)
        MegaApi::utf16ToUtf8(password->Data(), password->Length(), &utf8password);

    megaApi->decryptPasswordProtectedLink((link != nullptr) ? utf8link.c_str() : NULL,
        (password != nullptr) ? utf8password.c_str() : NULL);
}

void MegaSDK::encryptLinkWithPassword(String^ link, String^ password, MRequestListenerInterface^ listener)
{
    std::string utf8link;
    if (link != nullptr)
        MegaApi::utf16ToUtf8(link->Data(), link->Length(), &utf8link);

    std::string utf8password;
    if (password != nullptr)
        MegaApi::utf16ToUtf8(password->Data(), password->Length(), &utf8password);

    megaApi->encryptLinkWithPassword((link != nullptr) ? utf8link.c_str() : NULL,
        (password != nullptr) ? utf8password.c_str() : NULL,
        createDelegateMRequestListener(listener));
}

void MegaSDK::encryptLinkWithPassword(String^ link, String^ password)
{
    std::string utf8link;
    if (link != nullptr)
        MegaApi::utf16ToUtf8(link->Data(), link->Length(), &utf8link);

    std::string utf8password;
    if (password != nullptr)
        MegaApi::utf16ToUtf8(password->Data(), password->Length(), &utf8password);

    megaApi->encryptLinkWithPassword((link != nullptr) ? utf8link.c_str() : NULL,
        (password != nullptr) ? utf8password.c_str() : NULL);
}

void MegaSDK::getPublicNode(String^ megaFileLink, MRequestListenerInterface^ listener)
{
	std::string utf8megaFileLink;
	if (megaFileLink != nullptr)
		MegaApi::utf16ToUtf8(megaFileLink->Data(), megaFileLink->Length(), &utf8megaFileLink);

	megaApi->getPublicNode((megaFileLink != nullptr) ? utf8megaFileLink.c_str() : NULL,
		createDelegateMRequestListener(listener));
}

void MegaSDK::getPublicNode(String^ megaFileLink)
{
	std::string utf8megaFileLink;
	if (megaFileLink != nullptr)
		MegaApi::utf16ToUtf8(megaFileLink->Data(), megaFileLink->Length(), &utf8megaFileLink);

	megaApi->getPublicNode((megaFileLink != nullptr) ? utf8megaFileLink.c_str() : NULL);
}

void MegaSDK::getThumbnail(MNode^ node, String^ dstFilePath, MRequestListenerInterface^ listener)
{
	std::string utf8dstFilePath;
	if (dstFilePath != nullptr)
		MegaApi::utf16ToUtf8(dstFilePath->Data(), dstFilePath->Length(), &utf8dstFilePath);

	megaApi->getThumbnail((node != nullptr) ? node->getCPtr() : NULL, 
		(dstFilePath != nullptr) ? utf8dstFilePath.c_str() : NULL,
		createDelegateMRequestListener(listener));
}

void MegaSDK::getThumbnail(MNode^ node, String^ dstFilePath)
{
	std::string utf8dstFilePath;
	if (dstFilePath != nullptr)
		MegaApi::utf16ToUtf8(dstFilePath->Data(), dstFilePath->Length(), &utf8dstFilePath);

	megaApi->getThumbnail((node != nullptr) ? node->getCPtr() : NULL, 
		(dstFilePath != nullptr) ? utf8dstFilePath.c_str() : NULL);
}

void MegaSDK::cancelGetThumbnail(MNode^ node, MRequestListenerInterface^ listener)
{
	megaApi->cancelGetThumbnail((node != nullptr) ? node->getCPtr() : NULL,
		createDelegateMRequestListener(listener));
}

void MegaSDK::cancelGetThumbnail(MNode^ node)
{
	megaApi->cancelGetThumbnail((node != nullptr) ? node->getCPtr() : NULL);
}

void MegaSDK::setThumbnail(MNode^ node, String^ srcFilePath, MRequestListenerInterface^ listener)
{
	std::string utf8srcFilePath;
	if (srcFilePath != nullptr)
		MegaApi::utf16ToUtf8(srcFilePath->Data(), srcFilePath->Length(), &utf8srcFilePath);

	megaApi->setThumbnail((node != nullptr) ? node->getCPtr() : NULL,
		(srcFilePath != nullptr) ? utf8srcFilePath.c_str() : NULL,
		createDelegateMRequestListener(listener));
}

void MegaSDK::setThumbnail(MNode^ node, String^ srcFilePath)
{
	std::string utf8srcFilePath;
	if (srcFilePath != nullptr)
		MegaApi::utf16ToUtf8(srcFilePath->Data(), srcFilePath->Length(), &utf8srcFilePath);

	megaApi->setThumbnail((node != nullptr) ? node->getCPtr() : NULL,
		(srcFilePath != nullptr) ? utf8srcFilePath.c_str() : NULL);
}

void MegaSDK::getPreview(MNode^ node, String^ dstFilePath, MRequestListenerInterface^ listener)
{
	std::string utf8dstFilePath;
	if (dstFilePath != nullptr)
		MegaApi::utf16ToUtf8(dstFilePath->Data(), dstFilePath->Length(), &utf8dstFilePath);

	megaApi->getPreview((node != nullptr) ? node->getCPtr() : NULL, 
		(dstFilePath != nullptr) ? utf8dstFilePath.c_str() : NULL,
		createDelegateMRequestListener(listener));
}

void MegaSDK::getPreview(MNode^ node, String^ dstFilePath)
{
	std::string utf8dstFilePath;
	if (dstFilePath != nullptr)
		MegaApi::utf16ToUtf8(dstFilePath->Data(), dstFilePath->Length(), &utf8dstFilePath);

	megaApi->getPreview((node != nullptr) ? node->getCPtr() : NULL,
		(dstFilePath != nullptr) ? utf8dstFilePath.c_str() : NULL);
}

void MegaSDK::cancelGetPreview(MNode^ node, MRequestListenerInterface^ listener)
{
	megaApi->cancelGetPreview((node != nullptr) ? node->getCPtr() : NULL,
		createDelegateMRequestListener(listener));
}

void MegaSDK::cancelGetPreview(MNode^ node)
{
	megaApi->cancelGetPreview((node != nullptr) ? node->getCPtr() : NULL);
}

void MegaSDK::setPreview(MNode^ node, String^ srcFilePath, MRequestListenerInterface^ listener)
{
    std::string utf8srcFilePath;
    if (srcFilePath != nullptr)
        MegaApi::utf16ToUtf8(srcFilePath->Data(), srcFilePath->Length(), &utf8srcFilePath);

    megaApi->setPreview((node != nullptr) ? node->getCPtr() : NULL,
        (srcFilePath != nullptr) ? utf8srcFilePath.c_str() : NULL,
        createDelegateMRequestListener(listener));
}

void MegaSDK::setPreview(MNode^ node, String^ srcFilePath)
{
    std::string utf8srcFilePath;
    if (srcFilePath != nullptr)
        MegaApi::utf16ToUtf8(srcFilePath->Data(), srcFilePath->Length(), &utf8srcFilePath);

    megaApi->setPreview((node != nullptr) ? node->getCPtr() : NULL,
        (srcFilePath != nullptr) ? utf8srcFilePath.c_str() : NULL);
}

void MegaSDK::getUserAvatar(MUser^ user, String^ dstFilePath, MRequestListenerInterface^ listener)
{
    std::string utf8dstFilePath;
    if (dstFilePath != nullptr)
        MegaApi::utf16ToUtf8(dstFilePath->Data(), dstFilePath->Length(), &utf8dstFilePath);
    
    megaApi->getUserAvatar((user != nullptr) ? user->getCPtr() : NULL, 
        (dstFilePath != nullptr) ? utf8dstFilePath.c_str() : NULL,
        createDelegateMRequestListener(listener));
}

void MegaSDK::getUserAvatar(MUser^ user, String^ dstFilePath)
{
    std::string utf8dstFilePath;
    if (dstFilePath != nullptr)
        MegaApi::utf16ToUtf8(dstFilePath->Data(), dstFilePath->Length(), &utf8dstFilePath);

    megaApi->getUserAvatar((user != nullptr) ? user->getCPtr() : NULL, 
        (dstFilePath != nullptr) ? utf8dstFilePath.c_str() : NULL);
}

void MegaSDK::getOwnUserAvatar(String^ dstFilePath, MRequestListenerInterface^ listener)
{
    std::string utf8dstFilePath;
    if (dstFilePath != nullptr)
        MegaApi::utf16ToUtf8(dstFilePath->Data(), dstFilePath->Length(), &utf8dstFilePath);

    megaApi->getUserAvatar((dstFilePath != nullptr) ? utf8dstFilePath.c_str() : NULL,
        createDelegateMRequestListener(listener));
}

void MegaSDK::getOwnUserAvatar(String^ dstFilePath)
{
    std::string utf8dstFilePath;
    if (dstFilePath != nullptr)
        MegaApi::utf16ToUtf8(dstFilePath->Data(), dstFilePath->Length(), &utf8dstFilePath);

    megaApi->getUserAvatar((dstFilePath != nullptr) ? utf8dstFilePath.c_str() : NULL);
}

void MegaSDK::setAvatar(String ^srcFilePath, MRequestListenerInterface^ listener)
{
    std::string utf8srcFilePath;
    if (srcFilePath != nullptr)
        MegaApi::utf16ToUtf8(srcFilePath->Data(), srcFilePath->Length(), &utf8srcFilePath);

    megaApi->setAvatar((srcFilePath != nullptr) ? utf8srcFilePath.c_str() : NULL, 
        createDelegateMRequestListener(listener));
}

void MegaSDK::setAvatar(String ^srcFilePath)
{
    std::string utf8srcFilePath;
    if (srcFilePath != nullptr)
        MegaApi::utf16ToUtf8(srcFilePath->Data(), srcFilePath->Length(), &utf8srcFilePath);

    megaApi->setAvatar((srcFilePath != nullptr) ? utf8srcFilePath.c_str() : NULL);
}

String^ MegaSDK::getUserAvatarColor(MUser^ user)
{
    std::string utf16userAvatarColor;
    const char *utf8userAvatarColor = megaApi->getUserAvatarColor((user != nullptr) ? user->getCPtr() : NULL);
    if (!utf8userAvatarColor)
    {
        return nullptr;
    }

    MegaApi::utf8ToUtf16(utf8userAvatarColor, &utf16userAvatarColor);
    delete[] utf8userAvatarColor;

    return ref new String((wchar_t *)utf16userAvatarColor.data());
}

String^ MegaSDK::getUserHandleAvatarColor(String^ userhandle)
{
    std::string utf8userhandle;
    if (userhandle != nullptr)
        MegaApi::utf16ToUtf8(userhandle->Data(), userhandle->Length(), &utf8userhandle);

    std::string utf16userAvatarColor;
    const char *utf8userAvatarColor = megaApi->getUserAvatarColor((userhandle != nullptr) ? utf8userhandle.c_str() : NULL);
    if (!utf8userAvatarColor)
    {
        return nullptr;
    }

    MegaApi::utf8ToUtf16(utf8userAvatarColor, &utf16userAvatarColor);
    delete[] utf8userAvatarColor;

    return ref new String((wchar_t *)utf16userAvatarColor.data());
}

void MegaSDK::getUserAttribute(MUser^ user, int type, MRequestListenerInterface^ listener)
{
    megaApi->getUserAttribute((user != nullptr) ? user->getCPtr() : NULL, type,
        createDelegateMRequestListener(listener));
}

void MegaSDK::getUserAttribute(MUser^ user, int type)
{
    megaApi->getUserAttribute((user != nullptr) ? user->getCPtr() : NULL, type);
}

void MegaSDK::getUserEmail(MegaHandle handle, MRequestListenerInterface^ listener)
{
    if (handle == ::mega::INVALID_HANDLE) return;

    megaApi->getUserEmail(handle, createDelegateMRequestListener(listener));
}

void MegaSDK::getUserEmail(MegaHandle handle)
{
    if (handle == ::mega::INVALID_HANDLE) return;

    megaApi->getUserEmail(handle);
}

void MegaSDK::getOwnUserAttribute(int type, MRequestListenerInterface^ listener)
{
    megaApi->getUserAttribute(type, createDelegateMRequestListener(listener));
}

void MegaSDK::getOwnUserAttribute(int type)
{
    megaApi->getUserAttribute(type);
}

void MegaSDK::setUserAttribute(int type, String^ value, MRequestListenerInterface^ listener)
{
    std::string utf8value;
    if (value != nullptr)
        MegaApi::utf16ToUtf8(value->Data(), value->Length(), &utf8value);

    megaApi->setUserAttribute(type, (value != nullptr) ? utf8value.c_str() : NULL,
        createDelegateMRequestListener(listener));
}

void MegaSDK::setUserAttribute(int type, String^ value)
{
    std::string utf8value;
    if (value != nullptr)
        MegaApi::utf16ToUtf8(value->Data(), value->Length(), &utf8value);

    megaApi->setUserAttribute(type, (value != nullptr) ? utf8value.c_str() : NULL);
}

void MegaSDK::setCustomNodeAttribute(MNode^ node, String^ attrName, String^ value, MRequestListenerInterface^ listener)
{
    std::string utf8attrName;
    if (attrName != nullptr)
        MegaApi::utf16ToUtf8(attrName->Data(), attrName->Length(), &utf8attrName);

    std::string utf8value;
    if (value != nullptr)
        MegaApi::utf16ToUtf8(value->Data(), value->Length(), &utf8value);

    megaApi->setCustomNodeAttribute((node != nullptr) ? node->getCPtr() : NULL,
        (attrName != nullptr) ? utf8attrName.c_str() : NULL,
        (value != nullptr) ? utf8value.c_str() : NULL,
        createDelegateMRequestListener(listener));
}

void MegaSDK::setCustomNodeAttribute(MNode^ node, String^ attrName, String^ value)
{
    std::string utf8attrName;
    if (attrName != nullptr)
        MegaApi::utf16ToUtf8(attrName->Data(), attrName->Length(), &utf8attrName);

    std::string utf8value;
    if (value != nullptr)
        MegaApi::utf16ToUtf8(value->Data(), value->Length(), &utf8value);

    megaApi->setCustomNodeAttribute((node != nullptr) ? node->getCPtr() : NULL,
        (attrName != nullptr) ? utf8attrName.c_str() : NULL,
        (value != nullptr) ? utf8value.c_str() : NULL);
}

void MegaSDK::setNodeDuration(MNode^ node, int duration, MRequestListenerInterface^ listener)
{
    megaApi->setNodeDuration((node != nullptr) ? node->getCPtr() : NULL,
        duration, createDelegateMRequestListener(listener));
}

void MegaSDK::setNodeDuration(MNode^ node, int duration)
{
    megaApi->setNodeDuration((node != nullptr) ? node->getCPtr() : NULL, duration);
}

void MegaSDK::setNodeCoordinates(MNode^ node, double latitude, double longitude, MRequestListenerInterface^ listener)
{
    megaApi->setNodeCoordinates((node != nullptr) ? node->getCPtr() : NULL,
        latitude, longitude, createDelegateMRequestListener(listener));
}

void MegaSDK::setNodeCoordinates(MNode^ node, double latitude, double longitude)
{
    megaApi->setNodeCoordinates((node != nullptr) ? node->getCPtr() : NULL, latitude, longitude);
}

void MegaSDK::exportNode(MNode^ node, MRequestListenerInterface^ listener)
{
    megaApi->exportNode((node != nullptr) ? node->getCPtr() : NULL, 
        createDelegateMRequestListener(listener));
}

void MegaSDK::exportNode(MNode^ node)
{
    megaApi->exportNode((node != nullptr) ? node->getCPtr() : NULL);
}

void MegaSDK::exportNodeWithExpireTime(MNode^ node, int64 expireTime, MRequestListenerInterface^ listener)
{
    megaApi->exportNode((node != nullptr) ? node->getCPtr() : NULL, expireTime, 
        createDelegateMRequestListener(listener));
}

void MegaSDK::exportNodeWithExpireTime(MNode^ node, int64 expireTime)
{
    megaApi->exportNode((node != nullptr) ? node->getCPtr() : NULL, expireTime);
}

void MegaSDK::disableExport(MNode^ node, MRequestListenerInterface^ listener)
{
    megaApi->disableExport((node != nullptr) ? node->getCPtr() : NULL, 
        createDelegateMRequestListener(listener));
}

void MegaSDK::disableExport(MNode^ node)
{
    megaApi->disableExport((node != nullptr) ? node->getCPtr() : NULL);
}

void MegaSDK::fetchNodes(MRequestListenerInterface^ listener)
{
	megaApi->fetchNodes(createDelegateMRequestListener(listener));
}

void MegaSDK::fetchNodes()
{
	megaApi->fetchNodes();
}

void MegaSDK::getAccountDetails(MRequestListenerInterface^ listener)
{
	megaApi->getAccountDetails(createDelegateMRequestListener(listener));
}

void MegaSDK::getAccountDetails()
{
	megaApi->getAccountDetails();
}

void MegaSDK::getExtendedAccountDetails(bool sessions, bool purchases, bool transactions, MRequestListenerInterface^ listener)
{
    megaApi->getExtendedAccountDetails(sessions, purchases, transactions, createDelegateMRequestListener(listener));
}

void MegaSDK::getExtendedAccountDetails(bool sessions, bool purchases, bool transactions)
{
    megaApi->getExtendedAccountDetails(sessions, purchases, transactions);
}

void MegaSDK::queryTransferQuota(int64 size, MRequestListenerInterface^ listener)
{
    megaApi->queryTransferQuota(size, createDelegateMRequestListener(listener));
}

void MegaSDK::queryTransferQuota(int64 size)
{
    megaApi->queryTransferQuota(size);
}

void MegaSDK::getPricing(MRequestListenerInterface^ listener)
{
	megaApi->getPricing(createDelegateMRequestListener(listener));
}

void MegaSDK::getPricing()
{
	megaApi->getPricing();
}

void MegaSDK::getPaymentId(uint64 productHandle, MRequestListenerInterface^ listener)
{
	megaApi->getPaymentId(productHandle, createDelegateMRequestListener(listener));
}

void MegaSDK::getPaymentId(uint64 productHandle)
{
	megaApi->getPaymentId(productHandle);
}

void MegaSDK::upgradeAccount(uint64 productHandle, int paymentMethod, MRequestListenerInterface^ listener)
{
	megaApi->upgradeAccount(productHandle, paymentMethod, createDelegateMRequestListener(listener));
}

void MegaSDK::upgradeAccount(uint64 productHandle, int paymentMethod)
{
	megaApi->upgradeAccount(productHandle, paymentMethod);
}

void MegaSDK::submitPurchaseReceipt(int gateway, String^ receipt, MRequestListenerInterface^ listener)
{
    std::string utf8receipt;
    if (receipt != nullptr)
        MegaApi::utf16ToUtf8(receipt->Data(), receipt->Length(), &utf8receipt);

    megaApi->submitPurchaseReceipt(gateway, (receipt != nullptr) ? utf8receipt.c_str() : NULL,
        createDelegateMRequestListener(listener));
}

void MegaSDK::submitPurchaseReceipt(int gateway, String^ receipt)
{
    std::string utf8receipt;
    if (receipt != nullptr)
        MegaApi::utf16ToUtf8(receipt->Data(), receipt->Length(), &utf8receipt);

    megaApi->submitPurchaseReceipt(gateway, (receipt != nullptr) ? utf8receipt.c_str() : NULL);
}

void MegaSDK::creditCardStore(String^ address1, String^ address2, String^ city,
	String^ province, String^ country, String^ postalcode,
	String^ firstname, String^ lastname, String^ creditcard,
	String^ expire_month, String^ expire_year, String^ cv2,
	MRequestListenerInterface^ listener)
{
	std::string utf8address1;
	if (address1 != nullptr)
		MegaApi::utf16ToUtf8(address1->Data(), address1->Length(), &utf8address1);

	std::string utf8address2;
	if (address2 != nullptr)
		MegaApi::utf16ToUtf8(address2->Data(), address2->Length(), &utf8address2);

	std::string utf8city;
	if (city != nullptr)
		MegaApi::utf16ToUtf8(city->Data(), city->Length(), &utf8city);

	std::string utf8province;
	if (province != nullptr)
		MegaApi::utf16ToUtf8(province->Data(), province->Length(), &utf8province);

	std::string utf8country;
	if (country != nullptr)
		MegaApi::utf16ToUtf8(country->Data(), country->Length(), &utf8country);

	std::string utf8postalcode;
	if (postalcode != nullptr)
		MegaApi::utf16ToUtf8(postalcode->Data(), postalcode->Length(), &utf8postalcode);

	std::string utf8firstname;
	if (firstname != nullptr)
		MegaApi::utf16ToUtf8(firstname->Data(), firstname->Length(), &utf8firstname);

	std::string utf8lastname;
	if (lastname != nullptr)
		MegaApi::utf16ToUtf8(lastname->Data(), lastname->Length(), &utf8lastname);

	std::string utf8creditcard;
	if (creditcard != nullptr)
		MegaApi::utf16ToUtf8(creditcard->Data(), creditcard->Length(), &utf8creditcard);

	std::string utf8expire_month;
	if (expire_month != nullptr)
		MegaApi::utf16ToUtf8(expire_month->Data(), expire_month->Length(), &utf8expire_month);

	std::string utf8expire_year;
	if (expire_year != nullptr)
		MegaApi::utf16ToUtf8(expire_year->Data(), expire_year->Length(), &utf8expire_year);

	std::string utf8cv2;
	if (cv2 != nullptr)
		MegaApi::utf16ToUtf8(cv2->Data(), cv2->Length(), &utf8cv2);

	megaApi->creditCardStore((address1 != nullptr) ? utf8address1.c_str() : NULL,
		(address2 != nullptr) ? utf8address2.c_str() : NULL,
		(city != nullptr) ? utf8city.c_str() : NULL,
		(province != nullptr) ? utf8province.c_str() : NULL,
		(country != nullptr) ? utf8country.c_str() : NULL,
		(postalcode != nullptr) ? utf8postalcode.c_str() : NULL,
		(firstname != nullptr) ? utf8firstname.c_str() : NULL,
		(lastname != nullptr) ? utf8lastname.c_str() : NULL,
		(creditcard != nullptr) ? utf8creditcard.c_str() : NULL,
		(expire_month != nullptr) ? utf8expire_month.c_str() : NULL,
		(expire_year != nullptr) ? utf8expire_year.c_str() : NULL,
		(cv2 != nullptr) ? utf8cv2.c_str() : NULL, 
		createDelegateMRequestListener(listener));
}

void MegaSDK::creditCardStore(String^ address1, String^ address2, String^ city,
	String^ province, String^ country, String^ postalcode,
	String^ firstname, String^ lastname, String^ creditcard,
	String^ expire_month, String^ expire_year, String^ cv2)
{
	std::string utf8address1;
	if (address1 != nullptr)
		MegaApi::utf16ToUtf8(address1->Data(), address1->Length(), &utf8address1);

	std::string utf8address2;
	if (address2 != nullptr)
		MegaApi::utf16ToUtf8(address2->Data(), address2->Length(), &utf8address2);

	std::string utf8city;
	if (city != nullptr)
		MegaApi::utf16ToUtf8(city->Data(), city->Length(), &utf8city);

	std::string utf8province;
	if (province != nullptr)
		MegaApi::utf16ToUtf8(province->Data(), province->Length(), &utf8province);

	std::string utf8country;
	if (country != nullptr)
		MegaApi::utf16ToUtf8(country->Data(), country->Length(), &utf8country);

	std::string utf8postalcode;
	if (postalcode != nullptr)
		MegaApi::utf16ToUtf8(postalcode->Data(), postalcode->Length(), &utf8postalcode);

	std::string utf8firstname;
	if (firstname != nullptr)
		MegaApi::utf16ToUtf8(firstname->Data(), firstname->Length(), &utf8firstname);

	std::string utf8lastname;
	if (lastname != nullptr)
		MegaApi::utf16ToUtf8(lastname->Data(), lastname->Length(), &utf8lastname);

	std::string utf8creditcard;
	if (creditcard != nullptr)
		MegaApi::utf16ToUtf8(creditcard->Data(), creditcard->Length(), &utf8creditcard);

	std::string utf8expire_month;
	if (expire_month != nullptr)
		MegaApi::utf16ToUtf8(expire_month->Data(), expire_month->Length(), &utf8expire_month);

	std::string utf8expire_year;
	if (expire_year != nullptr)
		MegaApi::utf16ToUtf8(expire_year->Data(), expire_year->Length(), &utf8expire_year);

	std::string utf8cv2;
	if (cv2 != nullptr)
		MegaApi::utf16ToUtf8(cv2->Data(), cv2->Length(), &utf8cv2);

	megaApi->creditCardStore((address1 != nullptr) ? utf8address1.c_str() : NULL,
		(address2 != nullptr) ? utf8address2.c_str() : NULL,
		(city != nullptr) ? utf8city.c_str() : NULL,
		(province != nullptr) ? utf8province.c_str() : NULL,
		(country != nullptr) ? utf8country.c_str() : NULL,
		(postalcode != nullptr) ? utf8postalcode.c_str() : NULL,
		(firstname != nullptr) ? utf8firstname.c_str() : NULL,
		(lastname != nullptr) ? utf8lastname.c_str() : NULL,
		(creditcard != nullptr) ? utf8creditcard.c_str() : NULL,
		(expire_month != nullptr) ? utf8expire_month.c_str() : NULL,
		(expire_year != nullptr) ? utf8expire_year.c_str() : NULL,
		(cv2 != nullptr) ? utf8cv2.c_str() : NULL);
}

void MegaSDK::creditCardQuerySubscriptions(MRequestListenerInterface^ listener)
{
	megaApi->creditCardQuerySubscriptions(createDelegateMRequestListener(listener));
}

void MegaSDK::creditCardQuerySubscriptions()
{
	megaApi->creditCardQuerySubscriptions();
}

void MegaSDK::creditCardCancelSubscriptions(MRequestListenerInterface^ listener)
{
	megaApi->creditCardCancelSubscriptions(NULL, createDelegateMRequestListener(listener));
}

void MegaSDK::creditCardCancelSubscriptions(String^ reason, MRequestListenerInterface^ listener)
{
	std::string utf8reason;
	if (reason != nullptr)
		MegaApi::utf16ToUtf8(reason->Data(), reason->Length(), &utf8reason);

	megaApi->creditCardCancelSubscriptions((reason != nullptr) ? utf8reason.c_str() : NULL, createDelegateMRequestListener(listener));
}

void MegaSDK::creditCardCancelSubscriptions()
{
	megaApi->creditCardCancelSubscriptions(NULL);
}

void MegaSDK::getPaymentMethods(MRequestListenerInterface^ listener)
{
	megaApi->getPaymentMethods(createDelegateMRequestListener(listener));
}

void MegaSDK::getPaymentMethods()
{
	megaApi->getPaymentMethods();
}

String^ MegaSDK::exportMasterKey()
{
	std::string utf16key;
	const char *utf8key = megaApi->exportMasterKey();
	if (!utf8key)
	{
		return nullptr;
	}

	MegaApi::utf8ToUtf16(utf8key, &utf16key);
	delete[] utf8key;

	return ref new String((wchar_t *)utf16key.data());
}

void MegaSDK::masterKeyExported(MRequestListenerInterface^ listener)
{
    megaApi->masterKeyExported(createDelegateMRequestListener(listener));
}

void MegaSDK::masterKeyExported()
{
    megaApi->masterKeyExported();
}

void MegaSDK::changePassword(String^ oldPassword, String^ newPassword, MRequestListenerInterface^ listener)
{
	std::string utf8oldPassword;
	if (oldPassword != nullptr)
		MegaApi::utf16ToUtf8(oldPassword->Data(), oldPassword->Length(), &utf8oldPassword);

	std::string utf8newPassword;
	if (newPassword != nullptr)
		MegaApi::utf16ToUtf8(newPassword->Data(), newPassword->Length(), &utf8newPassword);

	megaApi->changePassword((oldPassword != nullptr) ? utf8oldPassword.c_str() : NULL,
		(newPassword != nullptr) ? utf8newPassword.c_str() : NULL,
		createDelegateMRequestListener(listener));
}

void MegaSDK::changePassword(String^ oldPassword, String^ newPassword)
{
	std::string utf8oldPassword;
	if (oldPassword != nullptr)
		MegaApi::utf16ToUtf8(oldPassword->Data(), oldPassword->Length(), &utf8oldPassword);

	std::string utf8newPassword;
	if (newPassword != nullptr)
		MegaApi::utf16ToUtf8(newPassword->Data(), newPassword->Length(), &utf8newPassword);

	megaApi->changePassword((oldPassword != nullptr) ? utf8oldPassword.c_str() : NULL,
		(newPassword != nullptr) ? utf8newPassword.c_str() : NULL);
}

void MegaSDK::inviteContact(String^ email, String^ message, MContactRequestInviteActionType action, MRequestListenerInterface^ listener)
{
    std::string utf8email;
    if (email != nullptr)
        MegaApi::utf16ToUtf8(email->Data(), email->Length(), &utf8email);

    std::string utf8message;
    if (message != nullptr)
        MegaApi::utf16ToUtf8(message->Data(), message->Length(), &utf8message);

    megaApi->inviteContact((email != nullptr) ? utf8email.c_str() : NULL,
        (message != nullptr) ? utf8message.c_str() : NULL, (int)action,
        createDelegateMRequestListener(listener));
}

void MegaSDK::inviteContact(String^ email, String^ message, MContactRequestInviteActionType action)
{
    std::string utf8email;
    if (email != nullptr)
        MegaApi::utf16ToUtf8(email->Data(), email->Length(), &utf8email);

    std::string utf8message;
    if (message != nullptr)
        MegaApi::utf16ToUtf8(message->Data(), message->Length(), &utf8message);

    megaApi->inviteContact((email != nullptr) ? utf8email.c_str() : NULL,
        (message != nullptr) ? utf8message.c_str() : NULL, (int)action);
}

void MegaSDK::replyContactRequest(MContactRequest^ request, MContactRequestReplyActionType action, MRequestListenerInterface^ listener)
{
    megaApi->replyContactRequest((request != nullptr) ? request->getCPtr() : NULL, (int)action,
        createDelegateMRequestListener(listener));
}

void MegaSDK::replyContactRequest(MContactRequest^ request, MContactRequestReplyActionType action)
{
    megaApi->replyContactRequest((request != nullptr) ? request->getCPtr() : NULL, (int)action);
}

void MegaSDK::removeContact(MUser ^user, MRequestListenerInterface^ listener)
{
    megaApi->removeContact((user != nullptr) ? user->getCPtr() : NULL,
        createDelegateMRequestListener(listener));
}

void MegaSDK::removeContact(MUser^ user)
{
    megaApi->removeContact((user != nullptr) ? user->getCPtr() : NULL);
}

void MegaSDK::logout(MRequestListenerInterface^ listener)
{
	megaApi->logout(createDelegateMRequestListener(listener));
}

void MegaSDK::logout()
{
	megaApi->logout();
}

void MegaSDK::localLogout(MRequestListenerInterface^ listener)
{
    megaApi->localLogout(createDelegateMRequestListener(listener));
}

void MegaSDK::localLogout()
{
    megaApi->localLogout();
}

int MegaSDK::getPasswordStrength(String^ password)
{
    std::string utf8password;
    if (password != nullptr)
        MegaApi::utf16ToUtf8(password->Data(), password->Length(), &utf8password);

    return megaApi->getPasswordStrength((password != nullptr) ? utf8password.c_str() : NULL);
}

void MegaSDK::submitFeedback(int rating, String^ comment, MRequestListenerInterface^ listener)
{
    std::string utf8comment;
    if (comment != nullptr)
        MegaApi::utf16ToUtf8(comment->Data(), comment->Length(), &utf8comment);

    megaApi->submitFeedback(rating,
        (comment != nullptr) ? utf8comment.c_str() : NULL,
        createDelegateMRequestListener(listener));
}

void MegaSDK::submitFeedback(int rating, String^ comment)
{
    std::string utf8comment;
    if (comment != nullptr)
        MegaApi::utf16ToUtf8(comment->Data(), comment->Length(), &utf8comment);

    megaApi->submitFeedback(rating,
        (comment != nullptr) ? utf8comment.c_str() : NULL);
}

void MegaSDK::reportDebugEvent(String^ text, MRequestListenerInterface^ listener)
{
    std::string utf8text;
    if (text != nullptr)
        MegaApi::utf16ToUtf8(text->Data(), text->Length(), &utf8text);

    megaApi->reportDebugEvent((text != nullptr) ? utf8text.c_str() : NULL,
        createDelegateMRequestListener(listener));
}

void MegaSDK::reportDebugEvent(String^ text)
{
    std::string utf8text;
    if (text != nullptr)
        MegaApi::utf16ToUtf8(text->Data(), text->Length(), &utf8text);

    megaApi->reportDebugEvent((text != nullptr) ? utf8text.c_str() : NULL);
}

void MegaSDK::startUpload(String^ localPath, MNode^ parent, MTransferListenerInterface^ listener)
{
	std::string utf8localPath;
	if (localPath != nullptr)
		MegaApi::utf16ToUtf8(localPath->Data(), localPath->Length(), &utf8localPath);

	megaApi->startUpload((localPath != nullptr) ? utf8localPath.c_str() : NULL,
		(parent != nullptr) ? parent->getCPtr() : NULL, 
		createDelegateMTransferListener(listener));
}

void MegaSDK::startUpload(String^ localPath, MNode^ parent)
{
	std::string utf8localPath;
	if (localPath != nullptr)
		MegaApi::utf16ToUtf8(localPath->Data(), localPath->Length(), &utf8localPath);

	megaApi->startUpload((localPath != nullptr) ? utf8localPath.c_str() : NULL,
		(parent != nullptr) ? parent->getCPtr() : NULL);
}

void MegaSDK::startUploadToFile(String^ localPath, MNode^ parent, String^ fileName, MTransferListenerInterface^ listener)
{
	std::string utf8localPath;
	if (localPath != nullptr)
		MegaApi::utf16ToUtf8(localPath->Data(), localPath->Length(), &utf8localPath);

	std::string utf8fileName;
	if (fileName != nullptr)
		MegaApi::utf16ToUtf8(fileName->Data(), fileName->Length(), &utf8fileName);

	megaApi->startUpload((localPath != nullptr) ? utf8localPath.c_str() : NULL,
		(parent != nullptr) ? parent->getCPtr() : NULL, 
		(fileName != nullptr) ? utf8fileName.c_str() : NULL,
		createDelegateMTransferListener(listener));
}

void MegaSDK::startUploadToFile(String^ localPath, MNode^ parent, String^ fileName)
{
	std::string utf8localPath;
	if (localPath != nullptr)
		MegaApi::utf16ToUtf8(localPath->Data(), localPath->Length(), &utf8localPath);

	std::string utf8fileName;
	if (fileName != nullptr)
		MegaApi::utf16ToUtf8(fileName->Data(), fileName->Length(), &utf8fileName);

	megaApi->startUpload((localPath != nullptr) ? utf8localPath.c_str() : NULL,
		(parent != nullptr) ? parent->getCPtr() : NULL, 
		(fileName != nullptr) ? utf8fileName.c_str() : NULL);
}

void MegaSDK::startUploadWithMtime(String^ localPath, MNode^ parent, uint64 mtime, MTransferListenerInterface^ listener)
{
	std::string utf8localPath;
	if (localPath != nullptr)
		MegaApi::utf16ToUtf8(localPath->Data(), localPath->Length(), &utf8localPath);

	megaApi->startUpload((localPath != nullptr) ? utf8localPath.c_str() : NULL,
		(parent != nullptr) ? parent->getCPtr() : NULL, mtime, createDelegateMTransferListener(listener));
}

void MegaSDK::startUploadWithMtime(String^ localPath, MNode^ parent, uint64 mtime)
{
    std::string utf8localPath;
    if (localPath != nullptr)
        MegaApi::utf16ToUtf8(localPath->Data(), localPath->Length(), &utf8localPath);

    megaApi->startUpload((localPath != nullptr) ? utf8localPath.c_str() : NULL,
        (parent != nullptr) ? parent->getCPtr() : NULL, mtime);
}

void MegaSDK::startUploadWithMtimeTempSource(String^ localPath, MNode^ parent, uint64 mtime, bool isSourceTemporary, MTransferListenerInterface^ listener)
{
    std::string utf8localPath;
    if (localPath != nullptr)
        MegaApi::utf16ToUtf8(localPath->Data(), localPath->Length(), &utf8localPath);

    megaApi->startUpload((localPath != nullptr) ? utf8localPath.c_str() : NULL,
        (parent != nullptr) ? parent->getCPtr() : NULL, mtime, isSourceTemporary,
        createDelegateMTransferListener(listener));
}

void MegaSDK::startUploadWithMtimeTempSource(String^ localPath, MNode^ parent, uint64 mtime, bool isSourceTemporary)
{
    std::string utf8localPath;
    if (localPath != nullptr)
        MegaApi::utf16ToUtf8(localPath->Data(), localPath->Length(), &utf8localPath);

    megaApi->startUpload((localPath != nullptr) ? utf8localPath.c_str() : NULL,
        (parent != nullptr) ? parent->getCPtr() : NULL, mtime, isSourceTemporary);
}

void MegaSDK::startUploadToFileWithMtime(String^ localPath, MNode^ parent, String^ fileName, uint64 mtime, MTransferListenerInterface^ listener)
{
    std::string utf8localPath;
    if (localPath != nullptr)
        MegaApi::utf16ToUtf8(localPath->Data(), localPath->Length(), &utf8localPath);

    std::string utf8fileName;
    if (fileName != nullptr)
        MegaApi::utf16ToUtf8(fileName->Data(), fileName->Length(), &utf8fileName);

    megaApi->startUpload((localPath != nullptr) ? utf8localPath.c_str() : NULL,
        (parent != nullptr) ? parent->getCPtr() : NULL,
        (fileName != nullptr) ? utf8fileName.c_str() : NULL, mtime,
        createDelegateMTransferListener(listener));
}

void MegaSDK::startUploadToFileWithMtime(String^ localPath, MNode^ parent, String^ fileName, uint64 mtime)
{
    std::string utf8localPath;
    if (localPath != nullptr)
        MegaApi::utf16ToUtf8(localPath->Data(), localPath->Length(), &utf8localPath);

    std::string utf8fileName;
    if (fileName != nullptr)
        MegaApi::utf16ToUtf8(fileName->Data(), fileName->Length(), &utf8fileName);

    megaApi->startUpload((localPath != nullptr) ? utf8localPath.c_str() : NULL,
        (parent != nullptr) ? parent->getCPtr() : NULL,
        (fileName != nullptr) ? utf8fileName.c_str() : NULL, mtime);
}

void MegaSDK::startUploadWithData(String^ localPath, MNode^ parent, String^ appData, MTransferListenerInterface^ listener)
{
    std::string utf8localPath;
    if (localPath != nullptr)
        MegaApi::utf16ToUtf8(localPath->Data(), localPath->Length(), &utf8localPath);

    std::string utf8appData;
    if (appData != nullptr)
        MegaApi::utf16ToUtf8(appData->Data(), appData->Length(), &utf8appData);

    megaApi->startUploadWithData((localPath != nullptr) ? utf8localPath.c_str() : NULL,
        (parent != nullptr) ? parent->getCPtr() : NULL,
        (appData != nullptr) ? utf8appData.c_str() : NULL,
        createDelegateMTransferListener(listener));
}

void MegaSDK::startUploadWithData(String^ localPath, MNode^ parent, String^ appData)
{
    std::string utf8localPath;
    if (localPath != nullptr)
        MegaApi::utf16ToUtf8(localPath->Data(), localPath->Length(), &utf8localPath);

    std::string utf8appData;
    if (appData != nullptr)
        MegaApi::utf16ToUtf8(appData->Data(), appData->Length(), &utf8appData);

    megaApi->startUploadWithData((localPath != nullptr) ? utf8localPath.c_str() : NULL,
        (parent != nullptr) ? parent->getCPtr() : NULL,
        (appData != nullptr) ? utf8appData.c_str() : NULL);
}

void MegaSDK::startUploadWithDataTempSource(String^ localPath, MNode^ parent, String^ appData, bool isSourceTemporary, MTransferListenerInterface^ listener)
{
    std::string utf8localPath;
    if (localPath != nullptr)
        MegaApi::utf16ToUtf8(localPath->Data(), localPath->Length(), &utf8localPath);

    std::string utf8appData;
    if (appData != nullptr)
        MegaApi::utf16ToUtf8(appData->Data(), appData->Length(), &utf8appData);

    megaApi->startUploadWithData((localPath != nullptr) ? utf8localPath.c_str() : NULL,
        (parent != nullptr) ? parent->getCPtr() : NULL,
        (appData != nullptr) ? utf8appData.c_str() : NULL, isSourceTemporary,
        createDelegateMTransferListener(listener));
}

void MegaSDK::startUploadWithDataTempSource(String^ localPath, MNode^ parent, String^ appData, bool isSourceTemporary)
{
    std::string utf8localPath;
    if (localPath != nullptr)
        MegaApi::utf16ToUtf8(localPath->Data(), localPath->Length(), &utf8localPath);

    std::string utf8appData;
    if (appData != nullptr)
        MegaApi::utf16ToUtf8(appData->Data(), appData->Length(), &utf8appData);

    megaApi->startUploadWithData((localPath != nullptr) ? utf8localPath.c_str() : NULL,
        (parent != nullptr) ? parent->getCPtr() : NULL,
        (appData != nullptr) ? utf8appData.c_str() : NULL, isSourceTemporary);
}

void MegaSDK::startDownload(MNode^ node, String^ localPath, MTransferListenerInterface^ listener)
{
	std::string utf8localPath;
	if (localPath != nullptr)
		MegaApi::utf16ToUtf8(localPath->Data(), localPath->Length(), &utf8localPath);

	megaApi->startDownload((node != nullptr) ? node->getCPtr() : NULL, 
		(localPath != nullptr) ? utf8localPath.c_str() : NULL,
		createDelegateMTransferListener(listener));
}

void MegaSDK::startDownload(MNode^ node, String^ localPath)
{
	std::string utf8localPath;
	if (localPath != nullptr)
		MegaApi::utf16ToUtf8(localPath->Data(), localPath->Length(), &utf8localPath);

	megaApi->startDownload((node != nullptr) ? node->getCPtr() : NULL,
		(localPath != nullptr) ? utf8localPath.c_str() : NULL);
}

void MegaSDK::startDownloadWithAppData(MNode^ node, String^ localPath, String^ appData, MTransferListenerInterface^ listener)
{
    std::string utf8localPath;
    if (localPath != nullptr)
        MegaApi::utf16ToUtf8(localPath->Data(), localPath->Length(), &utf8localPath);

    std::string utf8appData;
    if (appData != nullptr)
        MegaApi::utf16ToUtf8(appData->Data(), appData->Length(), &utf8appData);

    megaApi->startDownloadWithData((node != nullptr) ? node->getCPtr() : NULL,
        (localPath != nullptr) ? utf8localPath.c_str() : NULL,
        (appData != nullptr) ? utf8appData.c_str() : NULL,
        createDelegateMTransferListener(listener));
}

void MegaSDK::startDownloadWithAppData(MNode^ node, String^ localPath, String^ appData)
{
    std::string utf8localPath;
    if (localPath != nullptr)
        MegaApi::utf16ToUtf8(localPath->Data(), localPath->Length(), &utf8localPath);

    std::string utf8appData;
    if (appData != nullptr)
        MegaApi::utf16ToUtf8(appData->Data(), appData->Length(), &utf8appData);

    megaApi->startDownloadWithData((node != nullptr) ? node->getCPtr() : NULL,
        (localPath != nullptr) ? utf8localPath.c_str() : NULL,
        (appData != nullptr) ? utf8appData.c_str() : NULL);
}

void MegaSDK::startStreaming(MNode^ node, uint64 startPos, uint64 size, MTransferListenerInterface^ listener)
{
    megaApi->startStreaming((node != nullptr) ? node->getCPtr() : NULL,
        startPos, size, createDelegateMTransferListener(listener));
}

void MegaSDK::retryTransfer(MTransfer^ transfer, MTransferListenerInterface^ listener)
{
    megaApi->retryTransfer((transfer != nullptr) ? transfer->getCPtr() : NULL,
        createDelegateMTransferListener(listener));
}

void MegaSDK::retryTransfer(MTransfer^ transfer)
{
    megaApi->retryTransfer((transfer != nullptr) ? transfer->getCPtr() : NULL);
}

void MegaSDK::cancelTransfer(MTransfer^ transfer, MRequestListenerInterface^ listener)
{
    megaApi->cancelTransfer((transfer != nullptr) ? transfer->getCPtr() : NULL,
        createDelegateMRequestListener(listener));
}

void MegaSDK::cancelTransfer(MTransfer^ transfer)
{
    megaApi->cancelTransfer((transfer != nullptr) ? transfer->getCPtr() : NULL);
}

void MegaSDK::cancelTransferByTag(int transferTag, MRequestListenerInterface^ listener)
{
    megaApi->cancelTransferByTag(transferTag, createDelegateMRequestListener(listener));
}

void MegaSDK::cancelTransferByTag(int transferTag)
{
    megaApi->cancelTransferByTag(transferTag);
}

void MegaSDK::cancelTransfers(int direction, MRequestListenerInterface^ listener)
{
    megaApi->cancelTransfers(direction, createDelegateMRequestListener(listener));
}

void MegaSDK::cancelTransfers(int direction)
{
    megaApi->cancelTransfers(direction);
}

void MegaSDK::pauseTransfers(bool pause, MRequestListenerInterface^ listener)
{
    megaApi->pauseTransfers(pause, createDelegateMRequestListener(listener));
}

void MegaSDK::pauseTransfers(bool pause)
{
    megaApi->pauseTransfers(pause);
}

void MegaSDK::pauseTransfersDirection(bool pause, int direction, MRequestListenerInterface^ listener)
{
    megaApi->pauseTransfers(pause, direction, createDelegateMRequestListener(listener));
}

void MegaSDK::pauseTransfersDirection(bool pause, int direction)
{
    megaApi->pauseTransfers(pause, direction);
}

void MegaSDK::pauseTransfer(MTransfer^ transfer, bool pause, MRequestListenerInterface^ listener)
{
    megaApi->pauseTransfer((transfer != nullptr) ? transfer->getCPtr() : NULL,
        pause, createDelegateMRequestListener(listener));
}

void MegaSDK::pauseTransfer(MTransfer^ transfer, bool pause)
{
    megaApi->pauseTransfer((transfer != nullptr) ? transfer->getCPtr() : NULL, pause);
}

void MegaSDK::pauseTransferByTag(int transferTag, bool pause, MRequestListenerInterface^ listener)
{
    megaApi->pauseTransferByTag(transferTag, pause, createDelegateMRequestListener(listener));
}

void MegaSDK::pauseTransferByTag(int transferTag, bool pause)
{
    megaApi->pauseTransferByTag(transferTag, pause);
}

void MegaSDK::moveTransferUp(MTransfer^ transfer, MRequestListenerInterface^ listener)
{
    megaApi->moveTransferUp((transfer != nullptr) ? transfer->getCPtr() : NULL, 
        createDelegateMRequestListener(listener));
}

void MegaSDK::moveTransferUp(MTransfer^ transfer)
{
    megaApi->moveTransferUp((transfer != nullptr) ? transfer->getCPtr() : NULL);
}

void MegaSDK::moveTransferUpByTag(int transferTag, MRequestListenerInterface^ listener)
{
    megaApi->moveTransferUpByTag(transferTag, createDelegateMRequestListener(listener));
}

void MegaSDK::moveTransferUpByTag(int transferTag)
{
    megaApi->moveTransferUpByTag(transferTag);
}

void MegaSDK::moveTransferDown(MTransfer^ transfer, MRequestListenerInterface^ listener)
{
    megaApi->moveTransferDown((transfer != nullptr) ? transfer->getCPtr() : NULL, 
        createDelegateMRequestListener(listener));
}

void MegaSDK::moveTransferDown(MTransfer^ transfer)
{
    megaApi->moveTransferDown((transfer != nullptr) ? transfer->getCPtr() : NULL);
}

void MegaSDK::moveTransferDownByTag(int transferTag, MRequestListenerInterface^ listener)
{
    megaApi->moveTransferDownByTag(transferTag, createDelegateMRequestListener(listener));
}

void MegaSDK::moveTransferDownByTag(int transferTag)
{
    megaApi->moveTransferDownByTag(transferTag);
}

void MegaSDK::moveTransferToFirst(MTransfer^ transfer, MRequestListenerInterface^ listener)
{
    megaApi->moveTransferToFirst((transfer != nullptr) ? transfer->getCPtr() : NULL, 
        createDelegateMRequestListener(listener));
}

void MegaSDK::moveTransferToFirst(MTransfer^ transfer)
{
    megaApi->moveTransferToFirst((transfer != nullptr) ? transfer->getCPtr() : NULL);
}

void MegaSDK::moveTransferToFirstByTag(int transferTag, MRequestListenerInterface^ listener)
{
    megaApi->moveTransferToFirstByTag(transferTag, createDelegateMRequestListener(listener));
}

void MegaSDK::moveTransferToFirstByTag(int transferTag)
{
    megaApi->moveTransferToFirstByTag(transferTag);
}

void MegaSDK::moveTransferToLast(MTransfer^ transfer, MRequestListenerInterface^ listener)
{
    megaApi->moveTransferToLast((transfer != nullptr) ? transfer->getCPtr() : NULL, 
        createDelegateMRequestListener(listener));
}

void MegaSDK::moveTransferToLast(MTransfer^ transfer)
{
    megaApi->moveTransferToLast((transfer != nullptr) ? transfer->getCPtr() : NULL);
}

void MegaSDK::moveTransferToLastByTag(int transferTag, MRequestListenerInterface^ listener)
{
    megaApi->moveTransferToLastByTag(transferTag, createDelegateMRequestListener(listener));
}

void MegaSDK::moveTransferToLastByTag(int transferTag)
{
    megaApi->moveTransferToLastByTag(transferTag);
}

void MegaSDK::moveTransferBefore(MTransfer^ transfer, MTransfer^ prevTransfer, MRequestListenerInterface^ listener)
{
    megaApi->moveTransferBefore((transfer != nullptr) ? transfer->getCPtr() : NULL,
        (prevTransfer != nullptr) ? prevTransfer->getCPtr() : NULL,
        createDelegateMRequestListener(listener));
}

void MegaSDK::moveTransferBefore(MTransfer^ transfer, MTransfer^ prevTransfer)
{
    megaApi->moveTransferBefore((transfer != nullptr) ? transfer->getCPtr() : NULL,
        (prevTransfer != nullptr) ? prevTransfer->getCPtr() : NULL);
}

void MegaSDK::moveTransferBeforeByTag(int transferTag, int prevTransferTag, MRequestListenerInterface^ listener)
{
    megaApi->moveTransferBeforeByTag(transferTag, prevTransferTag, createDelegateMRequestListener(listener));
}

void MegaSDK::moveTransferBeforeByTag(int transferTag, int prevTransferTag)
{
    megaApi->moveTransferBeforeByTag(transferTag, prevTransferTag);
}

void MegaSDK::enableTransferResumption(String^ loggedOutId)
{
    std::string utf8loggedOutId;
    if (loggedOutId != nullptr)
        MegaApi::utf16ToUtf8(loggedOutId->Data(), loggedOutId->Length(), &utf8loggedOutId);

    megaApi->enableTransferResumption((loggedOutId != nullptr) ? utf8loggedOutId.c_str() : NULL);
}

void MegaSDK::enableTransferResumption()
{
    megaApi->enableTransferResumption();
}

void MegaSDK::disableTransferResumption(String^ loggedOutId)
{
    std::string utf8loggedOutId;
    if (loggedOutId != nullptr)
        MegaApi::utf16ToUtf8(loggedOutId->Data(), loggedOutId->Length(), &utf8loggedOutId);

    megaApi->disableTransferResumption((loggedOutId != nullptr) ? utf8loggedOutId.c_str() : NULL);
}

void MegaSDK::disableTransferResumption()
{
    megaApi->disableTransferResumption();
}

bool MegaSDK::areTransfersPaused(int direction)
{
    return megaApi->areTransfersPaused(direction);
}

void MegaSDK::setUploadLimit(int bpslimit)
{
	megaApi->setUploadLimit(bpslimit);
}

void MegaSDK::setDownloadMethod(int method)
{
    megaApi->setDownloadMethod(method);
}

void MegaSDK::setUploadMethod(int method)
{
    megaApi->setUploadMethod(method);
}

bool MegaSDK::setMaxDownloadSpeed(int64 bpslimit)
{
    return megaApi->setMaxDownloadSpeed(bpslimit);
}

bool MegaSDK::setMaxUploadSpeed(int64 bpslimit)
{
    return megaApi->setMaxUploadSpeed(bpslimit);
}

int MegaSDK::getMaxDownloadSpeed()
{
    return megaApi->getMaxDownloadSpeed();
}

int MegaSDK::getMaxUploadSpeed()
{
    return megaApi->getMaxUploadSpeed();
}

int MegaSDK::getCurrentDownloadSpeed()
{
    return megaApi->getCurrentDownloadSpeed();
}

int MegaSDK::getCurrentUploadSpeed()
{
    return megaApi->getCurrentUploadSpeed();
}

int MegaSDK::getCurrentSpeed(int type)
{
    return megaApi->getCurrentSpeed(type);
}

int MegaSDK::getDownloadMethod()
{
    return megaApi->getDownloadMethod();
}

int MegaSDK::getUploadMethod()
{
    return megaApi->getUploadMethod();
}

MTransferData^ MegaSDK::getTransferData(MTransferListenerInterface^ listener)
{
    return ref new MTransferData(megaApi->getTransferData(createDelegateMTransferListener(listener)), true);
}

MTransferData^ MegaSDK::getTransferData()
{
    return ref new MTransferData(megaApi->getTransferData(), true);
}

MTransfer^ MegaSDK::getFirstTransfer(int type)
{
    return ref new MTransfer(megaApi->getFirstTransfer(type), true);
}

void MegaSDK::notifyTransfer(MTransfer^ transfer, MTransferListenerInterface^ listener)
{
    megaApi->notifyTransfer((transfer != nullptr) ? transfer->getCPtr() : NULL, 
        createDelegateMTransferListener(listener));
}

void MegaSDK::notifyTransfer(MTransfer^ transfer)
{
    megaApi->notifyTransfer((transfer != nullptr) ? transfer->getCPtr() : NULL);
}

void MegaSDK::notifyTransferByTag(int transferTag, MTransferListenerInterface^ listener)
{
    megaApi->notifyTransferByTag(transferTag, createDelegateMTransferListener(listener));
}

void MegaSDK::notifyTransferByTag(int transferTag)
{
    megaApi->notifyTransferByTag(transferTag);
}

MTransferList^ MegaSDK::getTransfers()
{
    return ref new MTransferList(megaApi->getTransfers(), true);
}

MTransferList^ MegaSDK::getStreamingTransfers()
{
    return ref new MTransferList(megaApi->getStreamingTransfers(), true);
}

MTransfer^ MegaSDK::getTransferByTag(int transferTag)
{
    return ref new MTransfer(megaApi->getTransferByTag(transferTag), true);
}

MTransferList^ MegaSDK::getTransfers(MTransferType type)
{
    return ref new MTransferList(megaApi->getTransfers((int)type), true);
}

MTransferList^ MegaSDK::getChildTransfers(int transferTag)
{
    return ref new MTransferList(megaApi->getChildTransfers(transferTag), true);
}

bool MegaSDK::isWaiting()
{
    return megaApi->isWaiting();
}

bool MegaSDK::areServersBusy()
{
    return megaApi->areServersBusy();
}

int MegaSDK::getNumPendingUploads()
{
	return megaApi->getNumPendingUploads();
}

int MegaSDK::getNumPendingDownloads()
{
	return megaApi->getNumPendingDownloads();
}

int MegaSDK::getTotalUploads()
{
	return megaApi->getTotalUploads();
}

int MegaSDK::getTotalDownloads()
{
	return megaApi->getTotalDownloads();
}

void MegaSDK::resetTotalDownloads()
{
	megaApi->resetTotalDownloads();
}

void MegaSDK::resetTotalUploads()
{
	megaApi->resetTotalUploads();
}

void MegaSDK::updateStats()
{
    megaApi->updateStats();
}

uint64 MegaSDK::getNumNodes()
{
    return megaApi->getNumNodes();
}

uint64 MegaSDK::getTotalDownloadedBytes()
{
    return megaApi->getTotalDownloadedBytes();
}

uint64 MegaSDK::getTotalUploadedBytes()
{
    return megaApi->getTotalUploadedBytes();
}

uint64 MegaSDK::getTotalDownloadBytes()
{
    return megaApi->getTotalDownloadBytes();
}

uint64 MegaSDK::getTotalUploadBytes()
{
    return megaApi->getTotalUploadBytes();
}

int MegaSDK::getNumChildren(MNode^ parent)
{
	return megaApi->getNumChildren((parent != nullptr) ? parent->getCPtr() : NULL);
}

int MegaSDK::getNumChildFiles(MNode^ parent)
{
	return megaApi->getNumChildFiles((parent != nullptr) ? parent->getCPtr() : NULL);
}

int MegaSDK::getNumChildFolders(MNode^ parent)
{
	return megaApi->getNumChildFolders((parent != nullptr) ? parent->getCPtr() : NULL);
}

MNodeList^ MegaSDK::getChildren(MNode^ parent, int order)
{
	return ref new MNodeList(megaApi->getChildren((parent != nullptr) ? parent->getCPtr() : NULL, order), true);
}

MNodeList^ MegaSDK::getChildren(MNode^ parent)
{
	return ref new MNodeList(megaApi->getChildren((parent != nullptr) ? parent->getCPtr() : NULL), true);
}

MChildrenLists^ MegaSDK::getFileFolderChildren(MNode^ parent, int order)
{
    return ref new MChildrenLists(megaApi->getFileFolderChildren((parent != nullptr) ? parent->getCPtr() : NULL, order), true);
}

MChildrenLists^ MegaSDK::getFileFolderChildren(MNode^ parent)
{
    return ref new MChildrenLists(megaApi->getFileFolderChildren((parent != nullptr) ? parent->getCPtr() : NULL), true);
}

bool MegaSDK::hasChildren(MNode^ parent)
{
    return megaApi->hasChildren((parent != nullptr) ? parent->getCPtr() : NULL);
}

int MegaSDK::getIndex(MNode^ node, int order)
{
    return megaApi->getIndex((node != nullptr) ? node->getCPtr() : NULL, order);
}

int MegaSDK::getIndex(MNode^ node)
{
    return megaApi->getIndex((node != nullptr) ? node->getCPtr() : NULL);
}

MNode^ MegaSDK::getChildNode(MNode^ parent, String^ name)
{
	if (parent == nullptr || name == nullptr) return nullptr;

	std::string utf8name;
	MegaApi::utf16ToUtf8(name->Data(), name->Length(), &utf8name);

	MegaNode *node = megaApi->getChildNode(parent->getCPtr(), utf8name.c_str());
	return node ? ref new MNode(node, true) : nullptr;
}

MNode^ MegaSDK::getParentNode(MNode^ node)
{
	if (node == nullptr) return nullptr;

	MegaNode *parent = megaApi->getParentNode(node->getCPtr());
	return parent ? ref new MNode(parent, true) : nullptr;
}

String^ MegaSDK::getNodePath(MNode^ node)
{
    if (node == nullptr) return nullptr;
    
    std::string utf16path;
    const char *utf8path = megaApi->getNodePath(node->getCPtr());
    if (!utf8path)
    {
        return nullptr;
    }

    MegaApi::utf8ToUtf16(utf8path, &utf16path);
    delete[] utf8path;

    return ref new String((wchar_t *)utf16path.c_str());
}

MNode^ MegaSDK::getNodeByPath(String^ path, MNode^ n)
{
    if (path == nullptr || n == nullptr) return nullptr;

    std::string utf8path;
    MegaApi::utf16ToUtf8(path->Data(), path->Length(), &utf8path);

    MegaNode *node = megaApi->getNodeByPath(utf8path.c_str(), n->getCPtr());
    return node ? ref new MNode(node, true) : nullptr;
}

MNode^ MegaSDK::getNodeByPath(String^ path)
{
    if (path == nullptr) return nullptr;
    
    std::string utf8path;
    MegaApi::utf16ToUtf8(path->Data(), path->Length(), &utf8path);

    MegaNode *node = megaApi->getNodeByPath(utf8path.c_str());
    return node ? ref new MNode(node, true) : nullptr;
}

MNode^ MegaSDK::getNodeByHandle(uint64 handle)
{
    if (handle == ::mega::INVALID_HANDLE) return nullptr;

    MegaNode *node = megaApi->getNodeByHandle(handle);
    return node ? ref new MNode(node, true) : nullptr;
}

MNode^ MegaSDK::getNodeByBase64Handle(String^ base64Handle)
{
    if (base64Handle == nullptr) return nullptr;

    std::string utf8base64Handle;
    MegaApi::utf16ToUtf8(base64Handle->Data(), base64Handle->Length(), &utf8base64Handle);    

    MegaNode *node = megaApi->getNodeByHandle(MegaApi::base64ToHandle(utf8base64Handle.c_str()));
    return node ? ref new MNode(node, true) : nullptr;
}

MContactRequest^ MegaSDK::getContactRequestByHandle(MegaHandle handle)
{
    if (handle == ::mega::INVALID_HANDLE) return nullptr;

    MegaContactRequest *contactRequest = megaApi->getContactRequestByHandle(handle);
    return contactRequest ? ref new MContactRequest(contactRequest, true) : nullptr;
}

MUserList^ MegaSDK::getContacts()
{
    return ref new MUserList(megaApi->getContacts(), true);
}

MUser^ MegaSDK::getContact(String^ email)
{
    if (email == nullptr) return nullptr;

    std::string utf8email;
    MegaApi::utf16ToUtf8(email->Data(), email->Length(), &utf8email);

    MegaUser *user = megaApi->getContact(utf8email.c_str());
    return user ? ref new MUser(user, true) : nullptr;
}

MNodeList^ MegaSDK::getInShares(MUser^ user)
{
    return ref new MNodeList(megaApi->getInShares((user != nullptr) ? user->getCPtr() : NULL), true);
}

MNodeList^ MegaSDK::getInShares()
{
    return ref new MNodeList(megaApi->getInShares(), true);
}

MShareList^ MegaSDK::getInSharesList()
{
    return ref new MShareList(megaApi->getInSharesList(), true);
}

MUser^ MegaSDK::getUserFromInShare(MNode^ node)
{
    MegaUser *user = megaApi->getUserFromInShare((node != nullptr) ? node->getCPtr() : NULL);
    return user ? ref new MUser(user, true) : nullptr;
}

bool MegaSDK::isShared(MNode^ node)
{
    return megaApi->isShared((node != nullptr) ? node->getCPtr() : NULL);
}

bool MegaSDK::isOutShare(MNode^ node)
{
    return megaApi->isOutShare((node != nullptr) ? node->getCPtr() : NULL);
}

bool MegaSDK::isInShare(MNode^ node)
{
    return megaApi->isInShare((node != nullptr) ? node->getCPtr() : NULL);
}

bool MegaSDK::isPendingShare(MNode^ node)
{
    return megaApi->isPendingShare((node != nullptr) ? node->getCPtr() : NULL);
}

MShareList^ MegaSDK::getOutShares()
{
    return ref new MShareList(megaApi->getOutShares(), true);
}

MShareList^ MegaSDK::getOutShares(MNode^ node)
{
    return ref new MShareList(megaApi->getOutShares((node != nullptr) ? node->getCPtr() : NULL), true);
}

MShareList^ MegaSDK::getPendingOutShares()
{
    return ref new MShareList(megaApi->getPendingOutShares(), true);
}

MShareList^ MegaSDK::getPendingOutShares(MNode^ megaNode)
{
    return ref new MShareList(megaApi->getPendingOutShares((megaNode != nullptr) ? megaNode->getCPtr() : NULL), true);
}

MNodeList^ MegaSDK::getPublicLinks()
{
    return ref new MNodeList(megaApi->getPublicLinks(), true);
}

MContactRequestList^ MegaSDK::getIncomingContactRequests()
{
    return ref new MContactRequestList(megaApi->getIncomingContactRequests(), true);
}

MContactRequestList^ MegaSDK::getOutgoingContactRequests()
{
    return ref new MContactRequestList(megaApi->getOutgoingContactRequests(), true);
}

int MegaSDK::getAccess(MNode^ node)
{
    return megaApi->getAccess((node != nullptr) ? node->getCPtr() : NULL);
}

uint64 MegaSDK::getSize(MNode^ node)
{
    return megaApi->getSize((node != nullptr) ? node->getCPtr() : NULL);
}

void MegaSDK::removeRecursively(String^ path)
{
    std::string utf8path;
    if (path != nullptr)
        MegaApi::utf16ToUtf8(path->Data(), path->Length(), &utf8path);

    MegaApi::removeRecursively((path != nullptr) ? utf8path.c_str() : NULL);
}

String^ MegaSDK::getFileFingerprint(String^ filePath)
{
    if (filePath == nullptr) return nullptr;

    std::string utf8filePath;
    MegaApi::utf16ToUtf8(filePath->Data(), filePath->Length(), &utf8filePath);

    const char *utf8fingerprint = megaApi->getFingerprint(utf8filePath.c_str());
    if (!utf8fingerprint)
    {
        return nullptr;
    }

    std::string utf16fingerprint;
    MegaApi::utf8ToUtf16(utf8fingerprint, &utf16fingerprint);
    delete[] utf8fingerprint;

    return ref new String((wchar_t *)utf16fingerprint.c_str());
}

String^ MegaSDK::getFileFingerprint(MInputStream^ inputStream, uint64 mtime)
{
    if (inputStream == nullptr) return nullptr;

    MInputStreamAdapter is(inputStream);
    const char *utf8fingerprint = megaApi->getFingerprint(&is, mtime);
    if (!utf8fingerprint)
    {
        return nullptr;
    }

    std::string utf16fingerprint;
    MegaApi::utf8ToUtf16(utf8fingerprint, &utf16fingerprint);
    delete[] utf8fingerprint;

    return ref new String((wchar_t *)utf16fingerprint.c_str());
}

String^ MegaSDK::getNodeFingerprint(MNode ^node)
{
    if (node == nullptr) return nullptr;

    const char *utf8fingerprint = megaApi->getFingerprint(node->getCPtr());

    std::string utf16fingerprint;
    MegaApi::utf8ToUtf16(utf8fingerprint, &utf16fingerprint);
    delete[] utf8fingerprint;

    return utf8fingerprint ? ref new String((wchar_t *)utf16fingerprint.c_str()) : nullptr;
}

MNode^ MegaSDK::getNodeByFingerprint(String^ fingerprint)
{
    if (fingerprint == nullptr) return nullptr;

    std::string utf8fingerprint;
    MegaApi::utf16ToUtf8(fingerprint->Data(), fingerprint->Length(), &utf8fingerprint);

    MegaNode *node = megaApi->getNodeByFingerprint(utf8fingerprint.c_str());
    return node ? ref new MNode(node, true) : nullptr;
}

MNode^ MegaSDK::getNodeByFingerprint(String^ fingerprint, MNode^ parent)
{
    if (fingerprint == nullptr || parent == nullptr) return nullptr;

    std::string utf8fingerprint;
    MegaApi::utf16ToUtf8(fingerprint->Data(), fingerprint->Length(), &utf8fingerprint);

    MegaNode *node = megaApi->getNodeByFingerprint(utf8fingerprint.c_str(), parent->getCPtr());
    return node ? ref new MNode(node, true) : nullptr;
}

MNodeList^ MegaSDK::getNodesByFingerprint(String^ fingerprint)
{
    if (fingerprint == nullptr) return nullptr;

    std::string utf8fingerprint;
    MegaApi::utf16ToUtf8(fingerprint->Data(), fingerprint->Length(), &utf8fingerprint);

    MegaNodeList *nodes = megaApi->getNodesByFingerprint(utf8fingerprint.c_str());
    return nodes ? ref new MNodeList(nodes, true) : nullptr;
}

MNode^ MegaSDK::getExportableNodeByFingerprint(String^ fingerprint)
{
    if (fingerprint == nullptr) return nullptr;

    std::string utf8fingerprint;
    MegaApi::utf16ToUtf8(fingerprint->Data(), fingerprint->Length(), &utf8fingerprint);

    MegaNode *node = megaApi->getExportableNodeByFingerprint(utf8fingerprint.c_str());
    return node ? ref new MNode(node, true) : nullptr;
}

MNode^ MegaSDK::getExportableNodeByFingerprint(String^ fingerprint, String^ name)
{
    if (fingerprint == nullptr || name == nullptr) return nullptr;

    std::string utf8fingerprint;
    MegaApi::utf16ToUtf8(fingerprint->Data(), fingerprint->Length(), &utf8fingerprint);

    std::string utf8name;
    MegaApi::utf16ToUtf8(name->Data(), name->Length(), &utf8name);

    MegaNode *node = megaApi->getExportableNodeByFingerprint(utf8fingerprint.c_str(), utf8name.c_str());
    return node ? ref new MNode(node, true) : nullptr;
}

bool MegaSDK::hasFingerprint(String^ fingerprint)
{
    if (fingerprint == nullptr) return false;

    std::string utf8fingerprint;
    MegaApi::utf16ToUtf8(fingerprint->Data(), fingerprint->Length(), &utf8fingerprint);

    return megaApi->hasFingerprint(utf8fingerprint.c_str());
}

String^ MegaSDK::getCRCFromFile(String^ filePath)
{
    if (filePath == nullptr) return nullptr;

    std::string utf8filePath;
    MegaApi::utf16ToUtf8(filePath->Data(), filePath->Length(), &utf8filePath);

    return ref new String((wchar_t *)megaApi->getCRC(utf8filePath.c_str()));
}

String^ MegaSDK::getCRCFromFingerprint(String^ fingerprint)
{
    if (fingerprint == nullptr) return nullptr;

    std::string utf8fingerprint;
    MegaApi::utf16ToUtf8(fingerprint->Data(), fingerprint->Length(), &utf8fingerprint);

    return ref new String((wchar_t *)megaApi->getCRCFromFingerprint(utf8fingerprint.c_str()));
}

String^ MegaSDK::getCRCFromNode(MNode^ node)
{
    if (node == nullptr) return nullptr;

    return ref new String((wchar_t *)megaApi->getCRC((node != nullptr) ? node->getCPtr() : NULL));
}

MNode^ MegaSDK::getNodeByCRC(String^ crc, MNode^ parent)
{
    if (crc == nullptr || parent == nullptr) return nullptr;

    std::string utf8crc;
    MegaApi::utf16ToUtf8(crc->Data(), crc->Length(), &utf8crc);

    MegaNode *node = megaApi->getNodeByFingerprint(utf8crc.c_str(), parent->getCPtr());
    return node ? ref new MNode(node, true) : nullptr;
}

MError^ MegaSDK::checkAccess(MNode^ node, int level)
{
	if (node == nullptr) return nullptr;

	return ref new MError(megaApi->checkAccess(node->getCPtr(), level).copy(), true);
}

MError^ MegaSDK::checkMove(MNode^ node, MNode^ target)
{
	return ref new MError(megaApi->checkMove((node != nullptr) ? node->getCPtr() : NULL, (target != nullptr) ? target->getCPtr() : NULL).copy(), true);
}

bool MegaSDK::isFilesystemAvailable()
{
    return megaApi->isFilesystemAvailable();
}

MNode^ MegaSDK::getRootNode()
{
	MegaNode *node = megaApi->getRootNode();
	return node ? ref new MNode(node, true) : nullptr;
}

MNode^ MegaSDK::getRootNode(MNode^ node)
{
    MegaNode *rootNode = megaApi->getRootNode((node != nullptr) ? node->getCPtr() : NULL);
    return rootNode ? ref new MNode(rootNode, true) : nullptr;
}

MNode^ MegaSDK::getInboxNode()
{
    MegaNode *node = megaApi->getInboxNode();
    return node ? ref new MNode(node, true) : nullptr;
}

MNode^ MegaSDK::getRubbishNode()
{
	MegaNode *node = megaApi->getRubbishNode();
	return node ? ref new MNode(node, true) : nullptr;
}

bool MegaSDK::isInCloud(MNode^ node)
{
    return megaApi->isInCloud((node != nullptr) ? node->getCPtr() : NULL);
}

bool MegaSDK::isInRubbish(MNode^ node)
{
    return megaApi->isInRubbish((node != nullptr) ? node->getCPtr() : NULL);
}

bool MegaSDK::isInInbox(MNode^ node)
{
    return megaApi->isInInbox((node != nullptr) ? node->getCPtr() : NULL);
}

uint64 MegaSDK::getBandwidthOverquotaDelay()
{
    return megaApi->getBandwidthOverquotaDelay();
}

MNodeList^ MegaSDK::search(MNode^ node, String^ searchString, bool recursive)
{
    std::string utf8search;
    if (searchString != nullptr)
        MegaApi::utf16ToUtf8(searchString->Data(), searchString->Length(), &utf8search);

    return ref new MNodeList(megaApi->search(node->getCPtr(), (searchString != nullptr) ? utf8search.c_str() : NULL, recursive), true);
}

MNodeList^ MegaSDK::search(MNode^ node, String^ searchString)
{
    std::string utf8search;
    if (searchString != nullptr)
        MegaApi::utf16ToUtf8(searchString->Data(), searchString->Length(), &utf8search);

    return ref new MNodeList(megaApi->search(node->getCPtr(), (searchString != nullptr) ? utf8search.c_str() : NULL, true), true);
}

MNodeList^ MegaSDK::globalSearch(String^ searchString)
{
    std::string utf8search;
    if (searchString != nullptr)
        MegaApi::utf16ToUtf8(searchString->Data(), searchString->Length(), &utf8search);

    return ref new MNodeList(megaApi->search((searchString != nullptr) ? utf8search.c_str() : NULL), true);
}

bool MegaSDK::processMegaTree(MNode^ node, MTreeProcessorInterface^ processor, bool recursive)
{
    MegaTreeProcessor *delegateProcessor = createDelegateMTreeProcessor(processor);
    bool ret = megaApi->processMegaTree((node != nullptr) ? node->getCPtr() : NULL, delegateProcessor, recursive);
    delete delegateProcessor;
    return ret;
}

bool MegaSDK::processMegaTree(MNode^ node, MTreeProcessorInterface^ processor)
{
    MegaTreeProcessor *delegateProcessor = createDelegateMTreeProcessor(processor);
    bool ret = megaApi->processMegaTree((node != nullptr) ? node->getCPtr() : NULL, delegateProcessor, true);
    delete delegateProcessor;
    return ret;
}

MNode^ MegaSDK::authorizeNode(MNode^ node)
{
    return ref new MNode(megaApi->authorizeNode((node != nullptr) ? node->getCPtr() : NULL), true);
}

void MegaSDK::changeApiUrl(String^ apiURL, bool disablepkp)
{
    std::string utf8apiURL;
    if (apiURL != nullptr)
        MegaApi::utf16ToUtf8(apiURL->Data(), apiURL->Length(), &utf8apiURL);

    megaApi->changeApiUrl((apiURL != nullptr) ? utf8apiURL.c_str() : NULL, disablepkp);
}

void MegaSDK::changeApiUrl(String^ apiURL)
{
    std::string utf8apiURL;
    if (apiURL != nullptr)
        MegaApi::utf16ToUtf8(apiURL->Data(), apiURL->Length(), &utf8apiURL);

    megaApi->changeApiUrl((apiURL != nullptr) ? utf8apiURL.c_str() : NULL, false);
}

bool MegaSDK::setLanguage(String^ languageCode)
{
    std::string utf8languageCode;
    if (languageCode != nullptr)
        MegaApi::utf16ToUtf8(languageCode->Data(), languageCode->Length(), &utf8languageCode);

    return megaApi->setLanguage((languageCode != nullptr) ? utf8languageCode.c_str() : NULL);
}

bool MegaSDK::createThumbnail(String^ imagePath, String^ dstPath)
{
    std::string utf8imagePath;
    if (imagePath != nullptr)
        MegaApi::utf16ToUtf8(imagePath->Data(), imagePath->Length(), &utf8imagePath);

    std::string utf8dstPath;
    if (dstPath != nullptr)
        MegaApi::utf16ToUtf8(dstPath->Data(), dstPath->Length(), &utf8dstPath);

    return megaApi->createThumbnail((imagePath != nullptr) ? utf8imagePath.c_str() : NULL,
        (dstPath != nullptr) ? utf8dstPath.c_str() : NULL);
}

bool MegaSDK::createPreview(String^ imagePath, String^ dstPath)
{
    std::string utf8imagePath;
    if (imagePath != nullptr)
        MegaApi::utf16ToUtf8(imagePath->Data(), imagePath->Length(), &utf8imagePath);

    std::string utf8dstPath;
    if (dstPath != nullptr)
        MegaApi::utf16ToUtf8(dstPath->Data(), dstPath->Length(), &utf8dstPath);

    return megaApi->createPreview((imagePath != nullptr) ? utf8imagePath.c_str() : NULL,
        (dstPath != nullptr) ? utf8dstPath.c_str() : NULL);
}

bool MegaSDK::isOnline()
{
    return megaApi->isOnline();
}

void MegaSDK::getAccountAchievements(MRequestListenerInterface^ listener)
{
    megaApi->getAccountAchievements(createDelegateMRequestListener(listener));
}

void MegaSDK::getAccountAchievements()
{
    megaApi->getAccountAchievements();
}

void MegaSDK::getMegaAchievements(MRequestListenerInterface^ listener)
{
    megaApi->getMegaAchievements(createDelegateMRequestListener(listener));
}

void MegaSDK::getMegaAchievements()
{
    megaApi->getMegaAchievements();
}

MegaRequestListener *MegaSDK::createDelegateMRequestListener(MRequestListenerInterface^ listener, bool singleListener)
{
	if (listener == nullptr) return NULL;

	DelegateMRequestListener *delegateListener = new DelegateMRequestListener(this, listener, singleListener);
	EnterCriticalSection(&listenerMutex);
	activeRequestListeners.insert(delegateListener);
	LeaveCriticalSection(&listenerMutex);
	return delegateListener;
}

MegaTransferListener *MegaSDK::createDelegateMTransferListener(MTransferListenerInterface^ listener, bool singleListener)
{
	if (listener == nullptr) return NULL;

	DelegateMTransferListener *delegateListener = new DelegateMTransferListener(this, listener, singleListener);
	EnterCriticalSection(&listenerMutex);
	activeTransferListeners.insert(delegateListener);
	LeaveCriticalSection(&listenerMutex);
	return delegateListener;
}

MegaGlobalListener *MegaSDK::createDelegateMGlobalListener(MGlobalListenerInterface^ listener)
{
	if (listener == nullptr) return NULL;

	DelegateMGlobalListener *delegateListener = new DelegateMGlobalListener(this, listener);
	EnterCriticalSection(&listenerMutex);
	activeGlobalListeners.insert(delegateListener);
	LeaveCriticalSection(&listenerMutex);
	return delegateListener;
}

MegaListener *MegaSDK::createDelegateMListener(MListenerInterface^ listener)
{
	if (listener == nullptr) return NULL;

	DelegateMListener *delegateListener = new DelegateMListener(this, listener);
	EnterCriticalSection(&listenerMutex);
	activeMegaListeners.insert(delegateListener);
	LeaveCriticalSection(&listenerMutex);
	return delegateListener;
}

MegaTreeProcessor *MegaSDK::createDelegateMTreeProcessor(MTreeProcessorInterface^ processor)
{
	if (processor == nullptr) return NULL;

	DelegateMTreeProcessor *delegateProcessor = new DelegateMTreeProcessor(processor);
	return delegateProcessor;
}

void MegaSDK::freeRequestListener(DelegateMRequestListener *listener)
{
	if (listener == nullptr) return;

	EnterCriticalSection(&listenerMutex);
	activeRequestListeners.erase(listener);
	LeaveCriticalSection(&listenerMutex);
	delete listener;
}

void MegaSDK::freeTransferListener(DelegateMTransferListener *listener)
{
	if (listener == nullptr) return;

	EnterCriticalSection(&listenerMutex);
	activeTransferListeners.erase(listener);
	LeaveCriticalSection(&listenerMutex);
	delete listener;
}

MegaLogger *MegaSDK::createDelegateMLogger(MLoggerInterface^ logger)
{
    if (logger == nullptr) return NULL;

    DelegateMLogger *delegateLogger = new DelegateMLogger(logger);
    EnterCriticalSection(&loggerMutex);
    activeLoggers.insert(delegateLogger);
    LeaveCriticalSection(&loggerMutex);
    return delegateLogger;
}

void MegaSDK::freeLogger(DelegateMLogger *logger)
{
    if (logger == nullptr) return;
    delete logger;
}
