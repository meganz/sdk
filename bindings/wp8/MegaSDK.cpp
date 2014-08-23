#include "MegaSDK.h"

using namespace mega;
using namespace Platform;

MegaSDK::~MegaSDK()
{
	delete megaApi;
	DeleteCriticalSection(&listenerMutex);
}

MegaApi *MegaSDK::getCPtr()
{
	return megaApi;
}

MegaSDK::MegaSDK(String^ appKey, String^ userAgent)
{
	std::string utf8appKey;
	MegaApi::utf16ToUtf8(appKey->Data(), appKey->Length(), &utf8appKey);
	
	std::string utf8userAgent;
	MegaApi::utf16ToUtf8(userAgent->Data(), userAgent->Length(), &utf8userAgent);

	megaApi = new MegaApi(utf8appKey.data(), (const char *)NULL, utf8userAgent.data());
	InitializeCriticalSectionEx(&listenerMutex, 0, 0);
}

MegaSDK::MegaSDK(String^ appKey, String^ userAgent, String^ basePath)
{
	std::string utf8appKey;
	MegaApi::utf16ToUtf8(appKey->Data(), appKey->Length(), &utf8appKey);

	std::string utf8userAgent;
	MegaApi::utf16ToUtf8(userAgent->Data(), userAgent->Length(), &utf8userAgent);

	std::string utf8basePath;
	MegaApi::utf16ToUtf8(basePath->Data(), basePath->Length(), &utf8basePath);
		
	megaApi = new MegaApi(utf8appKey.data(), utf8basePath.data(), utf8userAgent.data());
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
	const char *utf8base64PwKey = megaApi->getBase64PwKey(utf8password.data());
	MegaApi::utf8ToUtf16(utf8base64PwKey, &utf16base64PwKey);
	delete[] utf8base64PwKey;

	return ref new String((wchar_t *)utf16base64PwKey.data());
}

String^ MegaSDK::getStringHash(String^ base64pwkey, String^ inBuf)
{
	if (base64pwkey == nullptr || inBuf == nullptr) return nullptr;

	std::string utf8base64pwkey;
	MegaApi::utf16ToUtf8(base64pwkey->Data(), base64pwkey->Length(), &utf8base64pwkey);

	std::string utf8inBuf;
	MegaApi::utf16ToUtf8(inBuf->Data(), inBuf->Length(), &utf8inBuf);

	std::string utf16stringHash;
	const char *utf8stringHash = megaApi->getStringHash(utf8base64pwkey.data(), utf8inBuf.data());
	MegaApi::utf8ToUtf16(utf8stringHash, &utf16stringHash);
	delete [] utf8stringHash;

	return ref new String((wchar_t *)utf16stringHash.data());
}

uint64 MegaSDK::base64ToHandle(String^ base64Handle)
{
	if (base64Handle == nullptr) return ::mega::INVALID_HANDLE;

	std::string utf8base64Handle;
	MegaApi::utf16ToUtf8(base64Handle->Data(), base64Handle->Length(), &utf8base64Handle);
	return MegaApi::base64ToHandle(utf8base64Handle.data());
}

String^ MegaSDK::ebcEncryptKey(String^ encryptionKey, String^ plainKey)
{
	if (encryptionKey == nullptr || plainKey == nullptr) return nullptr;

	std::string utf8encryptionKey;
	MegaApi::utf16ToUtf8(encryptionKey->Data(), encryptionKey->Length(), &utf8encryptionKey);

	std::string utf8plainKey;
	MegaApi::utf16ToUtf8(plainKey->Data(), plainKey->Length(), &utf8plainKey);

	const char *utf8ebcEncryptKey = MegaApi::ebcEncryptKey(utf8encryptionKey.data(), utf8plainKey.data());
	std::string utf16ebcEncryptKey;
	MegaApi::utf8ToUtf16(utf8ebcEncryptKey, &utf16ebcEncryptKey);
	delete[] utf8ebcEncryptKey;

	return ref new String((wchar_t *)utf16ebcEncryptKey.data());
}

void MegaSDK::retryPendingConnections()
{
	megaApi->retryPendingConnections();
}

void MegaSDK::login(String^ email, String^ password)
{
	std::string utf8email;
	MegaApi::utf16ToUtf8(email->Data(), email->Length(), &utf8email);

	std::string utf8password;
	MegaApi::utf16ToUtf8(password->Data(), password->Length(), &utf8password);

	megaApi->login(utf8email.data(), utf8password.data());
}

void MegaSDK::login(String^ email, String^ password, MRequestListenerInterface^ listener)
{
	std::string utf8email;
	MegaApi::utf16ToUtf8(email->Data(), email->Length(), &utf8email);

	std::string utf8password;
	MegaApi::utf16ToUtf8(password->Data(), password->Length(), &utf8password);

	megaApi->login(utf8email.data(), utf8password.data(), createDelegateMRequestListener(listener));
}

String^ MegaSDK::dumpSession()
{
	const char *utf8session = megaApi->dumpSession();
	std::string utf16session;
	MegaApi::utf8ToUtf16(utf8session, &utf16session);
	delete[] utf8session;

	return ref new String((wchar_t *)utf16session.data());
}

void MegaSDK::fastLogin(String^ email, String^ stringHash, String^ base64pwkey)
{
	std::string utf8email;
	MegaApi::utf16ToUtf8(email->Data(), email->Length(), &utf8email);

	std::string utf8stringHash;
	MegaApi::utf16ToUtf8(stringHash->Data(), stringHash->Length(), &utf8stringHash);

	std::string utf8base64pwkey;
	MegaApi::utf16ToUtf8(base64pwkey->Data(), base64pwkey->Length(), &utf8base64pwkey);

	megaApi->fastLogin(utf8email.data(), utf8stringHash.data(), utf8base64pwkey.data());
}

void MegaSDK::fastLogin(String^ email, String^ stringHash, String^ base64pwkey, MRequestListenerInterface^ listener)
{
	std::string utf8email;
	MegaApi::utf16ToUtf8(email->Data(), email->Length(), &utf8email);

	std::string utf8stringHash;
	MegaApi::utf16ToUtf8(stringHash->Data(), stringHash->Length(), &utf8stringHash);

	std::string utf8base64pwkey;
	MegaApi::utf16ToUtf8(base64pwkey->Data(), base64pwkey->Length(), &utf8base64pwkey);

	megaApi->fastLogin(utf8email.data(), utf8stringHash.data(), utf8base64pwkey.data(), createDelegateMRequestListener(listener));
}

void MegaSDK::fastLogin(String^ session)
{
	std::string utf8session;
	MegaApi::utf16ToUtf8(session->Data(), session->Length(), &utf8session);
	megaApi->fastLogin(utf8session.data());
}

void MegaSDK::fastLogin(String^ session, MRequestListenerInterface^ listener)
{
	std::string utf8session;
	MegaApi::utf16ToUtf8(session->Data(), session->Length(), &utf8session);
	megaApi->fastLogin(utf8session.data(), createDelegateMRequestListener(listener));
}

void MegaSDK::createAccount(String^ email, String^ password, String^ name)
{
	std::string utf8email;
	MegaApi::utf16ToUtf8(email->Data(), email->Length(), &utf8email);

	std::string utf8password;
	MegaApi::utf16ToUtf8(password->Data(), password->Length(), &utf8password);

	std::string utf8name;
	MegaApi::utf16ToUtf8(name->Data(), name->Length(), &utf8name);

	megaApi->createAccount(utf8email.data(), utf8password.data(), utf8name.data());
}

void MegaSDK::createAccount(String^ email, String^ password, String^ name, MRequestListenerInterface^ listener)
{
	std::string utf8email;
	MegaApi::utf16ToUtf8(email->Data(), email->Length(), &utf8email);

	std::string utf8password;
	MegaApi::utf16ToUtf8(password->Data(), password->Length(), &utf8password);

	std::string utf8name;
	MegaApi::utf16ToUtf8(name->Data(), name->Length(), &utf8name);

	megaApi->createAccount(utf8email.data(), utf8password.data(), utf8name.data(), createDelegateMRequestListener(listener));
}

void MegaSDK::fastCreateAccount(String^ email, String^ base64pwkey, String^ name)
{
	std::string utf8email;
	MegaApi::utf16ToUtf8(email->Data(), email->Length(), &utf8email);

	std::string utf8base64pwkey;
	MegaApi::utf16ToUtf8(base64pwkey->Data(), base64pwkey->Length(), &utf8base64pwkey);

	std::string utf8name;
	MegaApi::utf16ToUtf8(name->Data(), name->Length(), &utf8name);

	megaApi->fastCreateAccount(utf8email.data(), utf8base64pwkey.data(), utf8name.data());
}

void MegaSDK::fastCreateAccount(String^ email, String^ base64pwkey, String^ name, MRequestListenerInterface^ listener)
{
	std::string utf8email;
	MegaApi::utf16ToUtf8(email->Data(), email->Length(), &utf8email);

	std::string utf8base64pwkey;
	MegaApi::utf16ToUtf8(base64pwkey->Data(), base64pwkey->Length(), &utf8base64pwkey);

	std::string utf8name;
	MegaApi::utf16ToUtf8(name->Data(), name->Length(), &utf8name);

	megaApi->fastCreateAccount(utf8email.data(), utf8base64pwkey.data(), utf8name.data(), createDelegateMRequestListener(listener));
}

void MegaSDK::querySignupLink(String^ link)
{
	std::string utf8link;
	MegaApi::utf16ToUtf8(link->Data(), link->Length(), &utf8link);

	megaApi->querySignupLink(utf8link.data());
}

void MegaSDK::querySignupLink(String^ link, MRequestListenerInterface^ listener)
{
	std::string utf8link;
	MegaApi::utf16ToUtf8(link->Data(), link->Length(), &utf8link);

	megaApi->querySignupLink(utf8link.data(), createDelegateMRequestListener(listener));
}

void MegaSDK::confirmAccount(String^ link, String^ password)
{
	std::string utf8link;
	MegaApi::utf16ToUtf8(link->Data(), link->Length(), &utf8link);

	std::string utf8password;
	MegaApi::utf16ToUtf8(password->Data(), password->Length(), &utf8password);

	megaApi->confirmAccount(utf8link.data(), utf8password.data());
}

void MegaSDK::confirmAccount(String^ link, String^ password, MRequestListenerInterface^ listener)
{
	std::string utf8link;
	MegaApi::utf16ToUtf8(link->Data(), link->Length(), &utf8link);

	std::string utf8password;
	MegaApi::utf16ToUtf8(password->Data(), password->Length(), &utf8password);

	megaApi->confirmAccount(utf8link.data(), utf8password.data(), createDelegateMRequestListener(listener));
}

void MegaSDK::fastConfirmAccount(String^ link, String^ base64pwkey)
{
	std::string utf8link;
	MegaApi::utf16ToUtf8(link->Data(), link->Length(), &utf8link);

	std::string utf8base64pwkey;
	MegaApi::utf16ToUtf8(base64pwkey->Data(), base64pwkey->Length(), &utf8base64pwkey);

	megaApi->fastConfirmAccount(utf8link.data(), utf8base64pwkey.data());
}

void MegaSDK::fastConfirmAccount(String^ link, String^ base64pwkey, MRequestListenerInterface^ listener)
{
	std::string utf8link;
	MegaApi::utf16ToUtf8(link->Data(), link->Length(), &utf8link);

	std::string utf8base64pwkey;
	MegaApi::utf16ToUtf8(base64pwkey->Data(), base64pwkey->Length(), &utf8base64pwkey);

	megaApi->fastConfirmAccount(utf8link.data(), utf8base64pwkey.data(), createDelegateMRequestListener(listener));
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
	return ref new String((wchar_t *)utf16email.data());
}

void MegaSDK::createFolder(String^ name, MNode^ parent, MRequestListenerInterface^ listener)
{
	std::string utf8name;
	MegaApi::utf16ToUtf8(name->Data(), name->Length(), &utf8name);

	megaApi->createFolder(utf8name.data(), parent->getCPtr(), createDelegateMRequestListener(listener));
}

void MegaSDK::createFolder(String^ name, MNode^ parent)
{
	std::string utf8name;
	MegaApi::utf16ToUtf8(name->Data(), name->Length(), &utf8name);

	megaApi->createFolder(utf8name.data(), parent->getCPtr());
}

void MegaSDK::moveNode(MNode^ node, MNode^ newParent, MRequestListenerInterface^ listener)
{
	megaApi->moveNode(node->getCPtr(), newParent->getCPtr(), createDelegateMRequestListener(listener));
}

void MegaSDK::moveNode(MNode^ node, MNode^ newParent)
{
	megaApi->moveNode(node->getCPtr(), newParent->getCPtr());
}

void MegaSDK::copyNode(MNode^ node, MNode^ newParent, MRequestListenerInterface^ listener)
{
	megaApi->copyNode(node->getCPtr(), newParent->getCPtr(), createDelegateMRequestListener(listener));
}

void MegaSDK::copyNode(MNode^ node, MNode^ newParent)
{
	megaApi->copyNode(node->getCPtr(), newParent->getCPtr());
}

void MegaSDK::renameNode(MNode^ node, String^ newName, MRequestListenerInterface^ listener)
{
	std::string utf8newName;
	MegaApi::utf16ToUtf8(newName->Data(), newName->Length(), &utf8newName);

	megaApi->renameNode(node->getCPtr(), utf8newName.data(), createDelegateMRequestListener(listener));
}

void MegaSDK::renameNode(MNode^ node, String^ newName)
{
	std::string utf8newName;
	MegaApi::utf16ToUtf8(newName->Data(), newName->Length(), &utf8newName);

	megaApi->renameNode(node->getCPtr(), utf8newName.data());
}

void MegaSDK::remove(MNode^ node, MRequestListenerInterface^ listener)
{
	megaApi->remove(node->getCPtr(), createDelegateMRequestListener(listener));
}

void MegaSDK::remove(MNode^ node)
{
	megaApi->remove(node->getCPtr());
}

void MegaSDK::shareWithUser(MNode^ node, MUser^ user, int level, MRequestListenerInterface^ listener)
{
	megaApi->share(node->getCPtr(), user->getCPtr(), level, createDelegateMRequestListener(listener));
}

void MegaSDK::shareWithUser(MNode^ node, MUser^ user, int level)
{
	megaApi->share(node->getCPtr(), user->getCPtr(), level);
}

void MegaSDK::shareWithEmail(MNode^ node, String^ email, int level, MRequestListenerInterface^ listener)
{
	std::string utf8email;
	MegaApi::utf16ToUtf8(email->Data(), email->Length(), &utf8email);

	megaApi->share(node->getCPtr(), utf8email.data(), level, createDelegateMRequestListener(listener));
}

void MegaSDK::shareWithEmail(MNode^ node, String^ email, int level)
{
	std::string utf8email;
	MegaApi::utf16ToUtf8(email->Data(), email->Length(), &utf8email);

	megaApi->share(node->getCPtr(), utf8email.data(), level);
}

void MegaSDK::folderAccess(String^ megaFolderLink, MRequestListenerInterface^ listener)
{
	std::string utf8megaFolderLink;
	MegaApi::utf16ToUtf8(megaFolderLink->Data(), megaFolderLink->Length(), &utf8megaFolderLink);

	megaApi->folderAccess(utf8megaFolderLink.data(), createDelegateMRequestListener(listener));
}

void MegaSDK::folderAccess(String^ megaFolderLink)
{
	std::string utf8megaFolderLink;
	MegaApi::utf16ToUtf8(megaFolderLink->Data(), megaFolderLink->Length(), &utf8megaFolderLink);

	megaApi->folderAccess(utf8megaFolderLink.data());
}

void MegaSDK::importFileLink(String^ megaFileLink, MNode^ parent, MRequestListenerInterface^ listener)
{
	std::string utf8megaFileLink;
	MegaApi::utf16ToUtf8(megaFileLink->Data(), megaFileLink->Length(), &utf8megaFileLink);

	megaApi->importFileLink(utf8megaFileLink.data(), parent->getCPtr(), createDelegateMRequestListener(listener));
}

void MegaSDK::importFileLink(String^ megaFileLink, MNode^ parent)
{
	std::string utf8megaFileLink;
	MegaApi::utf16ToUtf8(megaFileLink->Data(), megaFileLink->Length(), &utf8megaFileLink);

	megaApi->importFileLink(utf8megaFileLink.data(), parent->getCPtr());
}

void MegaSDK::importPublicNode(MNode^ publicNode, MNode^ parent, MRequestListenerInterface^ listener)
{
	megaApi->importPublicNode(publicNode->getCPtr(), parent->getCPtr(), createDelegateMRequestListener(listener));
}

void MegaSDK::importPublicNode(MNode^ publicNode, MNode^ parent)
{
	megaApi->importPublicNode(publicNode->getCPtr(), parent->getCPtr());
}

void MegaSDK::getPublicNode(String^ megaFileLink, MRequestListenerInterface^ listener)
{
	std::string utf8megaFileLink;
	MegaApi::utf16ToUtf8(megaFileLink->Data(), megaFileLink->Length(), &utf8megaFileLink);

	megaApi->getPublicNode(utf8megaFileLink.data(), createDelegateMRequestListener(listener));
}

void MegaSDK::getPublicNode(String^ megaFileLink)
{
	std::string utf8megaFileLink;
	MegaApi::utf16ToUtf8(megaFileLink->Data(), megaFileLink->Length(), &utf8megaFileLink);

	megaApi->getPublicNode(utf8megaFileLink.data());
}

void MegaSDK::getThumbnail(MNode^ node, String^ dstFilePath, MRequestListenerInterface^ listener)
{
	std::string utf8dstFilePath;
	MegaApi::utf16ToUtf8(dstFilePath->Data(), dstFilePath->Length(), &utf8dstFilePath);

	megaApi->getThumbnail(node->getCPtr(), utf8dstFilePath.data(), createDelegateMRequestListener(listener));
}

void MegaSDK::getThumbnail(MNode^ node, String^ dstFilePath)
{
	std::string utf8dstFilePath;
	MegaApi::utf16ToUtf8(dstFilePath->Data(), dstFilePath->Length(), &utf8dstFilePath);

	megaApi->getThumbnail(node->getCPtr(), utf8dstFilePath.data());
}

void MegaSDK::setThumbnail(MNode^ node, String^ srcFilePath, MRequestListenerInterface^ listener)
{
	std::string utf8srcFilePath;
	MegaApi::utf16ToUtf8(srcFilePath->Data(), srcFilePath->Length(), &utf8srcFilePath);

	megaApi->setThumbnail(node->getCPtr(), utf8srcFilePath.data(), createDelegateMRequestListener(listener));
}

void MegaSDK::setThumbnail(MNode^ node, String^ srcFilePath)
{
	std::string utf8srcFilePath;
	MegaApi::utf16ToUtf8(srcFilePath->Data(), srcFilePath->Length(), &utf8srcFilePath);

	megaApi->setThumbnail(node->getCPtr(), utf8srcFilePath.data());
}

void MegaSDK::getPreview(MNode^ node, String^ dstFilePath, MRequestListenerInterface^ listener)
{
	std::string utf8dstFilePath;
	MegaApi::utf16ToUtf8(dstFilePath->Data(), dstFilePath->Length(), &utf8dstFilePath);

	megaApi->getPreview(node->getCPtr(), utf8dstFilePath.data(), createDelegateMRequestListener(listener));
}

void MegaSDK::getPreview(MNode^ node, String^ dstFilePath)
{
	std::string utf8dstFilePath;
	MegaApi::utf16ToUtf8(dstFilePath->Data(), dstFilePath->Length(), &utf8dstFilePath);

	megaApi->getPreview(node->getCPtr(), utf8dstFilePath.data());
}

void MegaSDK::setPreview(MNode^ node, String^ srcFilePath, MRequestListenerInterface^ listener)
{
	std::string utf8srcFilePath;
	MegaApi::utf16ToUtf8(srcFilePath->Data(), srcFilePath->Length(), &utf8srcFilePath);
	
	megaApi->setPreview(node->getCPtr(), utf8srcFilePath.data(), createDelegateMRequestListener(listener));
}

void MegaSDK::setPreview(MNode^ node, String^ srcFilePath)
{
	std::string utf8srcFilePath;
	MegaApi::utf16ToUtf8(srcFilePath->Data(), srcFilePath->Length(), &utf8srcFilePath);

	megaApi->setPreview(node->getCPtr(), utf8srcFilePath.data());
}

void MegaSDK::getUserAvatar(MUser^ user, String^ dstFilePath, MRequestListenerInterface^ listener)
{
	std::string utf8dstFilePath;
	MegaApi::utf16ToUtf8(dstFilePath->Data(), dstFilePath->Length(), &utf8dstFilePath);

	megaApi->getUserAvatar(user->getCPtr(), utf8dstFilePath.data(), createDelegateMRequestListener(listener));
}

void MegaSDK::getUserAvatar(MUser^ user, String^ dstFilePath)
{
	std::string utf8dstFilePath;
	MegaApi::utf16ToUtf8(dstFilePath->Data(), dstFilePath->Length(), &utf8dstFilePath);

	megaApi->getUserAvatar(user->getCPtr(), utf8dstFilePath.data());
}

void MegaSDK::exportNode(MNode^ node, MRequestListenerInterface^ listener)
{
	megaApi->exportNode(node->getCPtr(), createDelegateMRequestListener(listener));
}

void MegaSDK::exportNode(MNode^ node)
{
	megaApi->exportNode(node->getCPtr());
}

void MegaSDK::disableExport(MNode^ node, MRequestListenerInterface^ listener)
{
	megaApi->disableExport(node->getCPtr(), createDelegateMRequestListener(listener));
}

void MegaSDK::disableExport(MNode^ node)
{
	megaApi->disableExport(node->getCPtr());
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

void MegaSDK::changePassword(String^ oldPassword, String^ newPassword, MRequestListenerInterface^ listener)
{
	std::string utf8oldPassword;
	MegaApi::utf16ToUtf8(oldPassword->Data(), oldPassword->Length(), &utf8oldPassword);

	std::string utf8newPassword;
	MegaApi::utf16ToUtf8(newPassword->Data(), newPassword->Length(), &utf8newPassword);

	megaApi->changePassword(utf8oldPassword.data(), utf8newPassword.data(), createDelegateMRequestListener(listener));
}

void MegaSDK::changePassword(String^ oldPassword, String^ newPassword)
{
	std::string utf8oldPassword;
	MegaApi::utf16ToUtf8(oldPassword->Data(), oldPassword->Length(), &utf8oldPassword);

	std::string utf8newPassword;
	MegaApi::utf16ToUtf8(newPassword->Data(), newPassword->Length(), &utf8newPassword);

	megaApi->changePassword(utf8oldPassword.data(), utf8newPassword.data());
}

void MegaSDK::addContact(String^ email, MRequestListenerInterface^ listener)
{
	std::string utf8email;
	MegaApi::utf16ToUtf8(email->Data(), email->Length(), &utf8email);

	megaApi->addContact(utf8email.data(), createDelegateMRequestListener(listener));
}

void MegaSDK::addContact(String^ email)
{
	std::string utf8email;
	MegaApi::utf16ToUtf8(email->Data(), email->Length(), &utf8email);

	megaApi->addContact(utf8email.data());
}

void MegaSDK::removeContact(String^ email, MRequestListenerInterface^ listener)
{
	std::string utf8email;
	MegaApi::utf16ToUtf8(email->Data(), email->Length(), &utf8email);

	megaApi->removeContact(utf8email.data(), createDelegateMRequestListener(listener));
}

void MegaSDK::removeContact(String^ email)
{
	std::string utf8email;
	MegaApi::utf16ToUtf8(email->Data(), email->Length(), &utf8email);

	megaApi->removeContact(utf8email.data());
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
	MegaApi::utf16ToUtf8(localPath->Data(), localPath->Length(), &utf8localPath);

	megaApi->startUpload(utf8localPath.data(), parent->getCPtr(), createDelegateMTransferListener(listener));
}

void MegaSDK::startUpload(String^ localPath, MNode^ parent)
{
	std::string utf8localPath;
	MegaApi::utf16ToUtf8(localPath->Data(), localPath->Length(), &utf8localPath);

	megaApi->startUpload(utf8localPath.data(), parent->getCPtr());
}

void MegaSDK::startUploadToFile(String^ localPath, MNode^ parent, String^ fileName, MTransferListenerInterface^ listener)
{
	std::string utf8localPath;
	MegaApi::utf16ToUtf8(localPath->Data(), localPath->Length(), &utf8localPath);

	std::string utf8fileName;
	MegaApi::utf16ToUtf8(fileName->Data(), fileName->Length(), &utf8fileName);

	megaApi->startUpload(utf8localPath.data(), parent->getCPtr(), utf8fileName.data(), createDelegateMTransferListener(listener));
}

void MegaSDK::startUploadToFile(String^ localPath, MNode^ parent, String^ fileName)
{
	std::string utf8localPath;
	MegaApi::utf16ToUtf8(localPath->Data(), localPath->Length(), &utf8localPath);

	std::string utf8fileName;
	MegaApi::utf16ToUtf8(fileName->Data(), fileName->Length(), &utf8fileName);

	megaApi->startUpload(utf8localPath.data(), parent->getCPtr(), utf8fileName.data());
}

void MegaSDK::startDownload(MNode^ node, String^ localPath, MTransferListenerInterface^ listener)
{
	std::string utf8localPath;
	MegaApi::utf16ToUtf8(localPath->Data(), localPath->Length(), &utf8localPath);

	megaApi->startDownload(node->getCPtr(), utf8localPath.data(), createDelegateMTransferListener(listener));
}

void MegaSDK::startDownload(MNode^ node, String^ localPath)
{
	std::string utf8localPath;
	MegaApi::utf16ToUtf8(localPath->Data(), localPath->Length(), &utf8localPath);

	megaApi->startDownload(node->getCPtr(), utf8localPath.data());
}

void MegaSDK::startPublicDownload(MNode^ node, String^ localPath, MTransferListenerInterface^ listener)
{
	std::string utf8localPath;
	MegaApi::utf16ToUtf8(localPath->Data(), localPath->Length(), &utf8localPath);
	
	megaApi->startPublicDownload(node->getCPtr(), utf8localPath.data(), createDelegateMTransferListener(listener));
}

void MegaSDK::startPublicDownload(MNode^ node, String^ localPath)
{
	std::string utf8localPath;
	MegaApi::utf16ToUtf8(localPath->Data(), localPath->Length(), &utf8localPath);

	megaApi->startPublicDownload(node->getCPtr(), utf8localPath.data());
}

void MegaSDK::cancelTransfer(MTransfer^ transfer, MRequestListenerInterface^ listener)
{
	megaApi->cancelTransfer(transfer->getCPtr(), createDelegateMRequestListener(listener));
}

void MegaSDK::cancelTransfer(MTransfer^ transfer)
{
	megaApi->cancelTransfer(transfer->getCPtr());
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
	if (parent == nullptr) return 0;

	return megaApi->getNumChildren(parent->getCPtr());
}

int MegaSDK::getNumChildFiles(MNode^ parent)
{
	if (parent == nullptr) return 0;

	return megaApi->getNumChildFiles(parent->getCPtr());
}

int MegaSDK::getNumChildFolders(MNode^ parent)
{
	if (parent == nullptr) return 0;

	return megaApi->getNumChildFolders(parent->getCPtr());
}

MNodeList^ MegaSDK::getChildren(MNode^ parent, int order)
{
	if (parent == nullptr) return nullptr;

	return ref new MNodeList(megaApi->getChildren(parent->getCPtr(), order), true);
}

MNodeList^ MegaSDK::getChildren(MNode^ parent)
{
	if (parent == nullptr) return nullptr;

	return ref new MNodeList(megaApi->getChildren(parent->getCPtr()), true);
}

MNode^ MegaSDK::getChildNode(MNode^ parent, String^ name)
{
	if (parent == nullptr || name == nullptr) return nullptr;

	std::string utf8name;
	MegaApi::utf16ToUtf8(name->Data(), name->Length(), &utf8name);

	return ref new MNode(megaApi->getChildNode(parent->getCPtr(), utf8name.data()), true);
}

MNode^ MegaSDK::getParentNode(MNode^ node)
{
	return ref new MNode(megaApi->getParentNode(node->getCPtr()), true);
}

String^ MegaSDK::getNodePath(MNode^ node)
{
	if (node == nullptr) return nullptr;

	std::string utf16path;
	const char *utf8path = megaApi->getNodePath(node->getCPtr());
	MegaApi::utf8ToUtf16(utf8path, &utf16path);
	return ref new String((wchar_t *)utf16path.data());
}

MNode^ MegaSDK::getNodeByPath(String^ path, MNode^ n)
{
	if (path == nullptr || n == nullptr) return nullptr;

	std::string utf8path;
	MegaApi::utf16ToUtf8(path->Data(), path->Length(), &utf8path);

	return ref new MNode(megaApi->getNodeByPath(utf8path.data(), n->getCPtr()), true);
}

MNode^ MegaSDK::getNodeByPath(String^ path)
{
	if (path == nullptr) return nullptr;

	std::string utf8path;
	MegaApi::utf16ToUtf8(path->Data(), path->Length(), &utf8path);

	return ref new MNode(megaApi->getNodeByPath(utf8path.data()), true);
}

MNode^ MegaSDK::getNodeByHandle(uint64 handle)
{
	if (handle == ::mega::INVALID_HANDLE) return nullptr;

	return ref new MNode(megaApi->getNodeByHandle(handle), true);
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

	return ref new MUser(megaApi->getContact(utf8email.data()), true);
}

MNodeList^ MegaSDK::getInShares(MUser^ user)
{
	if (user == nullptr) return nullptr;

	return ref new MNodeList(megaApi->getInShares(user->getCPtr()), true);
}

MNodeList^ MegaSDK::getInShares()
{
	return ref new MNodeList(megaApi->getInShares(), true);
}

MShareList^ MegaSDK::getOutShares(MNode^ node)
{
	if (node == nullptr) return nullptr;

	return ref new MShareList(megaApi->getOutShares(node->getCPtr()), true);
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
	if (node == nullptr || target == nullptr) return nullptr;

	return ref new MError(megaApi->checkMove(node->getCPtr(), target->getCPtr()).copy(), true);
}

MNode^ MegaSDK::getRootNode()
{
	return ref new MNode(megaApi->getRootNode(), true);
}

MNode^ MegaSDK::getRubbishNode()
{
	return ref new MNode(megaApi->getRubbishNode(), true);
}


MegaRequestListener *MegaSDK::createDelegateMRequestListener(MRequestListenerInterface^ listener, bool singleListener)
{
	DelegateMRequestListener *delegateListener = new DelegateMRequestListener(this, listener, singleListener);
	EnterCriticalSection(&listenerMutex);
	activeRequestListeners.insert(delegateListener);
	LeaveCriticalSection(&listenerMutex);
	return delegateListener;
}

MegaTransferListener *MegaSDK::createDelegateMTransferListener(MTransferListenerInterface^ listener, bool singleListener)
{
	DelegateMTransferListener *delegateListener = new DelegateMTransferListener(this, listener, singleListener);
	EnterCriticalSection(&listenerMutex);
	activeTransferListeners.insert(delegateListener);
	LeaveCriticalSection(&listenerMutex);
	return delegateListener;
}

MegaGlobalListener *MegaSDK::createDelegateMGlobalListener(MGlobalListenerInterface^ listener)
{
	DelegateMGlobalListener *delegateListener = new DelegateMGlobalListener(this, listener);
	EnterCriticalSection(&listenerMutex);
	activeGlobalListeners.insert(delegateListener);
	LeaveCriticalSection(&listenerMutex);
	return delegateListener;
}

MegaListener *MegaSDK::createDelegateMListener(MListenerInterface^ listener)
{
	DelegateMListener *delegateListener = new DelegateMListener(this, listener);
	EnterCriticalSection(&listenerMutex);
	activeMegaListeners.insert(delegateListener);
	LeaveCriticalSection(&listenerMutex);
	return delegateListener;
}

void MegaSDK::freeRequestListener(DelegateMRequestListener *listener)
{
	EnterCriticalSection(&listenerMutex);
	activeRequestListeners.erase(listener);
	LeaveCriticalSection(&listenerMutex);
	delete listener;
}

void MegaSDK::freeTransferListener(DelegateMTransferListener *listener)
{
	EnterCriticalSection(&listenerMutex);
	activeTransferListeners.erase(listener);
	LeaveCriticalSection(&listenerMutex);
	delete listener;
}


/*MNodeList^ MegaSDK::search(MNode^ node, String^ searchString, bool recursive)
{
	std::string utf8search;
	MegaApi::utf16ToUtf8(searchString->Data(), searchString->Length(), &utf8search);

	return ref new MNodeList(megaApi->search(node->getCPtr(), utf8search.data(), recursive), true);
}

MNodeList^ MegaSDK::search(MNode^ node, String^ searchString)
{
	return nullptr;
}

bool MegaSDK::processMegaTree(MNode^ node, MTreeProcessorInterface^ processor, bool recursive)
{
	return false;
}

bool MegaSDK::processMegaTree(MNode^ node, MTreeProcessorInterface^ processor)
{
	return false;
}*/

 
