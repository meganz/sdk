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
	MegaApi::utf8ToUtf16(utf8base64PwKey, &utf16base64PwKey);
	delete[] utf8base64PwKey;

	return utf8base64PwKey ? ref new String((wchar_t *)utf16base64PwKey.c_str()) : nullptr;
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
	MegaApi::utf8ToUtf16(utf8stringHash, &utf16stringHash);
	delete [] utf8stringHash;

	return utf8stringHash ? ref new String((wchar_t *)utf16stringHash.c_str()) : nullptr;
}

uint64 MegaSDK::base64ToHandle(String^ base64Handle)
{
	if (base64Handle == nullptr) return ::mega::INVALID_HANDLE;

	std::string utf8base64Handle;
	MegaApi::utf16ToUtf8(base64Handle->Data(), base64Handle->Length(), &utf8base64Handle);
	return MegaApi::base64ToHandle(utf8base64Handle.c_str());
}

String^ MegaSDK::ebcEncryptKey(String^ encryptionKey, String^ plainKey)
{
	if (encryptionKey == nullptr || plainKey == nullptr) return nullptr;

	std::string utf8encryptionKey;
	MegaApi::utf16ToUtf8(encryptionKey->Data(), encryptionKey->Length(), &utf8encryptionKey);

	std::string utf8plainKey;
	MegaApi::utf16ToUtf8(plainKey->Data(), plainKey->Length(), &utf8plainKey);

	const char *utf8ebcEncryptKey = MegaApi::ebcEncryptKey(utf8encryptionKey.c_str(), utf8plainKey.c_str());
	std::string utf16ebcEncryptKey;
	MegaApi::utf8ToUtf16(utf8ebcEncryptKey, &utf16ebcEncryptKey);
	delete[] utf8ebcEncryptKey;

	return utf8ebcEncryptKey ? ref new String((wchar_t *)utf16ebcEncryptKey.c_str()) : nullptr;
}

void MegaSDK::retryPendingConnections()
{
	megaApi->retryPendingConnections();
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
	std::string utf16session;
	MegaApi::utf8ToUtf16(utf8session, &utf16session);
	delete[] utf8session;

	return utf8session ? ref new String((wchar_t *)utf16session.c_str()) : nullptr;
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
	MegaApi::utf8ToUtf16(utf8email, &utf16email);
	delete[] utf8email;

	return utf8email ? ref new String((wchar_t *)utf16email.c_str()) : nullptr;
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

void MegaSDK::shareWithUser(MNode^ node, MUser^ user, int level, MRequestListenerInterface^ listener)
{
	megaApi->share((node != nullptr) ? node->getCPtr() : NULL, 
		(user != nullptr) ? user->getCPtr() : NULL, level, 
		createDelegateMRequestListener(listener));
}

void MegaSDK::shareWithUser(MNode^ node, MUser^ user, int level)
{
	megaApi->share((node != nullptr) ? node->getCPtr() : NULL, 
		(user != nullptr) ? user->getCPtr() : NULL, level);
}

void MegaSDK::shareWithEmail(MNode^ node, String^ email, int level, MRequestListenerInterface^ listener)
{
	std::string utf8email;
	if (email != nullptr)
		MegaApi::utf16ToUtf8(email->Data(), email->Length(), &utf8email);

	megaApi->share((node != nullptr) ? node->getCPtr() : NULL, 
		(email != nullptr) ? utf8email.c_str() : NULL, level,
		createDelegateMRequestListener(listener));
}

void MegaSDK::shareWithEmail(MNode^ node, String^ email, int level)
{
	std::string utf8email;
	if (email != nullptr)
		MegaApi::utf16ToUtf8(email->Data(), email->Length(), &utf8email);

	megaApi->share((node != nullptr) ? node->getCPtr() : NULL, 
		(email != nullptr) ? utf8email.c_str() : NULL, level);
}

void MegaSDK::folderAccess(String^ megaFolderLink, MRequestListenerInterface^ listener)
{
	std::string utf8megaFolderLink;
	if (megaFolderLink != nullptr)
		MegaApi::utf16ToUtf8(megaFolderLink->Data(), megaFolderLink->Length(), &utf8megaFolderLink);

	megaApi->folderAccess((megaFolderLink != nullptr) ? utf8megaFolderLink.c_str() : NULL,
		createDelegateMRequestListener(listener));
}

void MegaSDK::folderAccess(String^ megaFolderLink)
{
	std::string utf8megaFolderLink;
	if (megaFolderLink != nullptr)
		MegaApi::utf16ToUtf8(megaFolderLink->Data(), megaFolderLink->Length(), &utf8megaFolderLink);

	megaApi->folderAccess((megaFolderLink != nullptr) ? utf8megaFolderLink.c_str() : nullptr);
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

void MegaSDK::importPublicNode(MNode^ publicNode, MNode^ parent, MRequestListenerInterface^ listener)
{
	megaApi->importPublicNode((publicNode != nullptr) ? publicNode->getCPtr() : NULL, 
		(parent != nullptr) ? parent->getCPtr() : NULL, 
		createDelegateMRequestListener(listener));
}

void MegaSDK::importPublicNode(MNode^ publicNode, MNode^ parent)
{
	megaApi->importPublicNode((publicNode != nullptr) ? publicNode->getCPtr() : NULL, 
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

void MegaSDK::getPaymentUrl(uint64 productHandle, MRequestListenerInterface^ listener)
{
	megaApi->getPaymentUrl(productHandle, createDelegateMRequestListener(listener));
}

void MegaSDK::getPaymentUrl(uint64 productHandle)
{
	megaApi->getPaymentUrl(productHandle);
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

void MegaSDK::removeContact(String^ email, MRequestListenerInterface^ listener)
{
	std::string utf8email;
	if (email != nullptr)
		MegaApi::utf16ToUtf8(email->Data(), email->Length(), &utf8email);

	megaApi->removeContact((email != nullptr) ? utf8email.c_str() : NULL,
		createDelegateMRequestListener(listener));
}

void MegaSDK::removeContact(String^ email)
{
	std::string utf8email;
	if (email != nullptr)
		MegaApi::utf16ToUtf8(email->Data(), email->Length(), &utf8email);

	megaApi->removeContact((email != nullptr) ? utf8email.c_str() : NULL);
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

void MegaSDK::startPublicDownload(MNode^ node, String^ localPath, MTransferListenerInterface^ listener)
{
	std::string utf8localPath;
	if (localPath != nullptr)
		MegaApi::utf16ToUtf8(localPath->Data(), localPath->Length(), &utf8localPath);
	
	megaApi->startPublicDownload((node != nullptr) ? node->getCPtr() : NULL,
		(localPath != nullptr) ? utf8localPath.c_str() : NULL,
		createDelegateMTransferListener(listener));
}

void MegaSDK::startPublicDownload(MNode^ node, String^ localPath)
{
	std::string utf8localPath;
	if (localPath != nullptr)
		MegaApi::utf16ToUtf8(localPath->Data(), localPath->Length(), &utf8localPath);

	megaApi->startPublicDownload((node != nullptr) ? node->getCPtr() : NULL,
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
	MegaApi::utf8ToUtf16(utf8path, &utf16path);
	delete[] utf8path;

	return utf8path ? ref new String((wchar_t *)utf16path.c_str()) : nullptr;
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

String^ MegaSDK::getFileFingerprint(String^ filePath)
{
	if (filePath == nullptr) return nullptr;

	std::string utf8filePath;
	MegaApi::utf16ToUtf8(filePath->Data(), filePath->Length(), &utf8filePath);

	const char *utf8fingerprint = megaApi->getFingerprint(utf8filePath.c_str());
	
	std::string utf16fingerprint;
	MegaApi::utf8ToUtf16(utf8fingerprint, &utf16fingerprint);
	delete[] utf8fingerprint;

	return utf8fingerprint ? ref new String((wchar_t *)utf16fingerprint.c_str()) : nullptr;
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

void MegaSDK::setLoggerClass(MLoggerInterface^ megaLogger)
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
