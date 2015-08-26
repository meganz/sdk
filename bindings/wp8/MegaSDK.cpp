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

DelegateMLogger* MegaSDK::externalLogger = new DelegateMLogger(nullptr);

MegaSDK::~MegaSDK()
{
	delete megaApi;
	DeleteCriticalSection(&listenerMutex);
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
}

void MegaSDK::addListener(MListenerInterface^ listener)
{
	megaApi->addListener(createDelegateMListener(listener));
}

void MegaSDK::addRequestListener(MRequestListenerInterface^ listener)
{
	megaApi->addRequestListener(createDelegateMRequestListener(listener, false));
}

void MegaSDK::addMTransferListener(MTransferListenerInterface^ listener)
{
	megaApi->addTransferListener(createDelegateMTransferListener(listener, false));
}

void MegaSDK::addGlobalListener(MGlobalListenerInterface^ listener)
{
	megaApi->addGlobalListener(createDelegateMGlobalListener(listener));
}

void MegaSDK::removeListener(MListenerInterface^ listener)
{
	EnterCriticalSection(&listenerMutex);
	std::set<DelegateMListener *>::iterator it = activeMegaListeners.begin();
	while (it != activeMegaListeners.end())
	{
		DelegateMListener *delegate = *it;
		if (delegate->getUserListener() == listener)
		{
			megaApi->removeListener(delegate);
			activeMegaListeners.erase(it++);
		}
		else it++;
	}
	LeaveCriticalSection(&listenerMutex);
}

void MegaSDK::removeRequestListener(MRequestListenerInterface^ listener)
{
	EnterCriticalSection(&listenerMutex);
	std::set<DelegateMRequestListener *>::iterator it = activeRequestListeners.begin();
	while (it != activeRequestListeners.end())
	{
		DelegateMRequestListener *delegate = *it;
		if (delegate->getUserListener() == listener)
		{
			megaApi->removeRequestListener(delegate);
			activeRequestListeners.erase(it++);
		}
		else it++;
	}
	LeaveCriticalSection(&listenerMutex);
}

void MegaSDK::removeTransferListener(MTransferListenerInterface^ listener)
{
	EnterCriticalSection(&listenerMutex);
	std::set<DelegateMTransferListener *>::iterator it = activeTransferListeners.begin();
	while (it != activeTransferListeners.end())
	{
		DelegateMTransferListener *delegate = *it;
		if (delegate->getUserListener() == listener)
		{
			megaApi->removeTransferListener(delegate);
			activeTransferListeners.erase(it++);
		}
		else it++;
	}
	LeaveCriticalSection(&listenerMutex);
}

void MegaSDK::removeGlobalListener(MGlobalListenerInterface^ listener)
{
	EnterCriticalSection(&listenerMutex);
	std::set<DelegateMGlobalListener *>::iterator it = activeGlobalListeners.begin();
	while (it != activeGlobalListeners.end())
	{
		DelegateMGlobalListener *delegate = *it;
		if (delegate->getUserListener() == listener)
		{
			megaApi->removeGlobalListener(delegate);
			activeGlobalListeners.erase(it++);
		}
		else it++;
	}
	LeaveCriticalSection(&listenerMutex);
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

uint64 MegaSDK::base64ToHandle(String^ base64Handle)
{
	if (base64Handle == nullptr) return ::mega::INVALID_HANDLE;

	std::string utf8base64Handle;
	MegaApi::utf16ToUtf8(base64Handle->Data(), base64Handle->Length(), &utf8base64Handle);
	return MegaApi::base64ToHandle(utf8base64Handle.c_str());
}

void MegaSDK::retryPendingConnections()
{
	megaApi->retryPendingConnections();
}

void MegaSDK::reconnect()
{
	megaApi->retryPendingConnections(true, true);
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

void MegaSDK::createAccount(String^ email, String^ password, String^ name)
{
	std::string utf8email;
	if (email != nullptr)
		MegaApi::utf16ToUtf8(email->Data(), email->Length(), &utf8email);

	std::string utf8password;
	if (password != nullptr)
		MegaApi::utf16ToUtf8(password->Data(), password->Length(), &utf8password);

	std::string utf8name;
	if (name != nullptr)
		MegaApi::utf16ToUtf8(name->Data(), name->Length(), &utf8name);

	megaApi->createAccount((email != nullptr) ? utf8email.c_str() : NULL,
		(password != nullptr) ? utf8password.c_str() : NULL,
		(name != nullptr) ? utf8name.c_str() : NULL);
}

void MegaSDK::createAccount(String^ email, String^ password, String^ name, MRequestListenerInterface^ listener)
{
	std::string utf8email;
	if (email != nullptr)
		MegaApi::utf16ToUtf8(email->Data(), email->Length(), &utf8email);

	std::string utf8password;
	if (password != nullptr)
		MegaApi::utf16ToUtf8(password->Data(), password->Length(), &utf8password);

	std::string utf8name;
	if (name != nullptr)
		MegaApi::utf16ToUtf8(name->Data(), name->Length(), &utf8name);

	megaApi->createAccount((email != nullptr) ? utf8email.c_str() : NULL,
		(password != nullptr) ? utf8password.c_str() : NULL,
		(name != nullptr) ? utf8name.c_str() : NULL,
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
		(email != nullptr) ? utf8base64pwkey.c_str() : NULL,
		(base64pwkey != nullptr) ? utf8name.c_str() : NULL,
		createDelegateMRequestListener(listener));
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

void MegaSDK::getUserAttribute(MUser^ user, int type, MRequestListenerInterface^ listener)
{
    megaApi->getUserAttribute((user != nullptr) ? user->getCPtr() : NULL, type,
        createDelegateMRequestListener(listener));
}

void MegaSDK::getUserAttribute(MUser^ user, int type)
{
    megaApi->getUserAttribute((user != nullptr) ? user->getCPtr() : NULL, type);
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

void MegaSDK::exportNode(MNode^ node, MRequestListenerInterface^ listener)
{
    megaApi->exportNode((node != nullptr) ? node->getCPtr() : NULL, 
        createDelegateMRequestListener(listener));
}

void MegaSDK::exportNode(MNode^ node)
{
    megaApi->exportNode((node != nullptr) ? node->getCPtr() : NULL);
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

void MegaSDK::submitPurchaseReceipt(String^ receipt, MRequestListenerInterface^ listener)
{
	std::string utf8receipt;
	if (receipt != nullptr)
		MegaApi::utf16ToUtf8(receipt->Data(), receipt->Length(), &utf8receipt);

	megaApi->submitPurchaseReceipt((receipt != nullptr) ? utf8receipt.c_str() : NULL,
		createDelegateMRequestListener(listener));
}

void MegaSDK::submitPurchaseReceipt(String^ receipt)
{
	std::string utf8receipt;
	if (receipt != nullptr)
		MegaApi::utf16ToUtf8(receipt->Data(), receipt->Length(), &utf8receipt);

	megaApi->submitPurchaseReceipt((receipt != nullptr) ? utf8receipt.c_str() : NULL);
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

void MegaSDK::addContact(String^ email, MRequestListenerInterface^ listener)
{
    std::string utf8email;
    if (email != nullptr)
        MegaApi::utf16ToUtf8(email->Data(), email->Length(), &utf8email);

    megaApi->addContact((email != nullptr) ? utf8email.c_str() : NULL,
        createDelegateMRequestListener(listener));
}

void MegaSDK::addContact(String^ email)
{
    std::string utf8email;
    if (email != nullptr)
        MegaApi::utf16ToUtf8(email->Data(), email->Length(), &utf8email);

    megaApi->addContact((email != nullptr) ? utf8email.c_str() : NULL);
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

void MegaSDK::startStreaming(MNode^ node, uint64 startPos, uint64 size, MTransferListenerInterface^ listener)
{
	megaApi->startStreaming((node != nullptr) ? node->getCPtr() : NULL, startPos, size, createDelegateMTransferListener(listener));
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

void MegaSDK::setUploadLimit(int bpslimit)
{
	megaApi->setUploadLimit(bpslimit);
}

MTransferList^ MegaSDK::getTransfers()
{
	return ref new MTransferList(megaApi->getTransfers(), true);
}

MTransferList^ MegaSDK::getTransfers(MTransferType type)
{
	return ref new MTransferList(megaApi->getTransfers((int)type), true);
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

uint64 MegaSDK::getTotalDownloadedBytes()
{
	return megaApi->getTotalDownloadedBytes();
}

uint64 MegaSDK::getTotalUploadedBytes()
{
	return megaApi->getTotalUploadedBytes();
}

void MegaSDK::resetTotalDownloads()
{
	megaApi->resetTotalDownloads();
}

void MegaSDK::resetTotalUploads()
{
	megaApi->resetTotalUploads();
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

int MegaSDK::getIndex(MNode^ node, int order)
{
    return megaApi->getIndex((node != nullptr) ? node->getCPtr() : NULL, order);
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

bool MegaSDK::isShared(MNode^ node)
{
    return megaApi->isShared(node->getCPtr());
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

MContactRequestList^ MegaSDK::getIncomingContactRequests()
{
    return ref new MContactRequestList(megaApi->getIncomingContactRequests(), true);
}

MContactRequestList^ MegaSDK::getOutgoingContactRequests()
{
    return ref new MContactRequestList(megaApi->getOutgoingContactRequests(), true);
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

bool MegaSDK::hasFingerprint(String^ fingerprint)
{
    if (fingerprint == nullptr) return false;

    std::string utf8fingerprint;
    MegaApi::utf16ToUtf8(fingerprint->Data(), fingerprint->Length(), &utf8fingerprint);

    return megaApi->hasFingerprint(utf8fingerprint.c_str());
}

int MegaSDK::getAccess(MNode^ node)
{
	if (node == nullptr) return -1;

	return megaApi->getAccess(node->getCPtr());
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

MNode^ MegaSDK::getRootNode()
{
	MegaNode *node = megaApi->getRootNode();
	return node ? ref new MNode(node, true) : nullptr;
}

MNode^ MegaSDK::getRubbishNode()
{
	MegaNode *node = megaApi->getRubbishNode();
	return node ? ref new MNode(node, true) : nullptr;
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

void MegaSDK::setLogLevel(MLogLevel logLevel)
{
	MegaApi::setLogLevel((int)logLevel);
}

void MegaSDK::setLoggerObject(MLoggerInterface^ megaLogger)
{
	DelegateMLogger *newLogger = new DelegateMLogger(megaLogger);
	delete externalLogger;
	externalLogger = newLogger;
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
