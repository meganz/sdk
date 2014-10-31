#pragma once

#include <Windows.h>
#include <Synchapi.h>
#include <string>

#include "MNode.h"
#include "MUser.h"
#include "MTransfer.h"
#include "MRequest.h"
#include "MError.h"
#include "MTransferList.h"
#include "MNodeList.h"
#include "MUserList.h"
#include "MShareList.h"
#include "MListenerInterface.h"
#include "MRequestListenerInterface.h"
#include "MTransferListenerInterface.h"
#include "MGlobalListenerInterface.h"
#include "MTreeProcessorInterface.h"
#include "DelegateMRequestListener.h"
#include "DelegateMTransferListener.h"
#include "DelegateMGlobalListener.h"
#include "DelegateMListener.h"
#include "DelegateMTreeProcessor.h"
#include "DelegateMGfxProcessor.h"
#include "DelegateMLogger.h"
#include "MRandomNumberProvider.h"

#include <megaapi.h>
#include <set>

namespace mega
{
	using namespace Windows::Foundation;
	using Platform::String;

	public enum class MSortOrderType {
		ORDER_NONE, ORDER_DEFAULT_ASC, ORDER_DEFAULT_DESC,
		ORDER_SIZE_ASC, ORDER_SIZE_DESC,
		ORDER_CREATION_ASC, ORDER_CREATION_DESC,
		ORDER_MODIFICATION_ASC, ORDER_MODIFICATION_DESC,
		ORDER_ALPHABETICAL_ASC, ORDER_ALPHABETICAL_DESC
	};

	public enum class MLogLevel {
		LOG_LEVEL_FATAL = 0, 
		LOG_LEVEL_ERROR,   // Error information but will continue application to keep running.
		LOG_LEVEL_WARNING, // Information representing errors in application but application will keep running
		LOG_LEVEL_INFO,    // Mainly useful to represent current progress of application.
		LOG_LEVEL_DEBUG,   // Informational logs, that are useful for developers. Only applicable if DEBUG is defined.
		LOG_LEVEL_MAX
	};

	public ref class MegaSDK sealed
	{
		friend class DelegateMRequestListener;
		friend class DelegateMGlobalListener;
		friend class DelegateMTransferListener;
		friend class DelegateMListener;

	public:
		MegaSDK(String^ appKey, String^ userAgent, MRandomNumberProvider ^randomProvider);
		MegaSDK(String^ appKey, String^ userAgent, String^ basePath, MRandomNumberProvider^ randomProvider);
		MegaSDK(String^ appKey, String^ userAgent, String^ basePath, MRandomNumberProvider^ randomProvider, MGfxProcessorInterface^ gfxProcessor);
		virtual ~MegaSDK();
		void addListener(MListenerInterface^ listener);
		void addRequestListener(MRequestListenerInterface^ listener);
		void addMTransferListener(MTransferListenerInterface^ listener);
		void addGlobalListener(MGlobalListenerInterface^ listener);
		void removeListener(MListenerInterface^ listener);
		void removeRequestListener(MRequestListenerInterface^ listener);
		void removeTransferListener(MTransferListenerInterface^ listener);
		void removeGlobalListener(MGlobalListenerInterface^ listener);
		String^ getBase64PwKey(String^ password);
		String^ getStringHash(String^ base64pwkey, String^ inBuf);
		static uint64 base64ToHandle(String^ base64Handle);
		static String^ ebcEncryptKey(String^ encryptionKey, String^ plainKey);
		void retryPendingConnections();
		void login(String^ email, String^ password, MRequestListenerInterface^ listener);
		void login(String^ email, String^ password);
		String^ dumpSession();
		void fastLogin(String^ email, String^ stringHash, String^ base64pwkey, MRequestListenerInterface^ listener);
		void fastLogin(String^ email, String^ stringHash, String^ base64pwkey);
		void fastLogin(String^ session, MRequestListenerInterface^ listener);
		void fastLogin(String^ session);
		void createAccount(String^ email, String^ password, String^ name, MRequestListenerInterface^ listener);
		void createAccount(String^ email, String^ password, String^ name);
		void fastCreateAccount(String^ email, String^ base64pwkey, String^ name, MRequestListenerInterface^ listener);
		void fastCreateAccount(String^ email, String^ base64pwkey, String^ name);
		void querySignupLink(String^ link, MRequestListenerInterface^ listener);
		void querySignupLink(String^ link);
		void confirmAccount(String^ link, String^ password, MRequestListenerInterface^ listener);
		void confirmAccount(String^ link, String^ password);
		void fastConfirmAccount(String^ link, String^ base64pwkey, MRequestListenerInterface^ listener);
		void fastConfirmAccount(String^ link, String^ base64pwkey);
		int isLoggedIn();
		String^ getMyEmail();
		void createFolder(String^ name, MNode^ parent, MRequestListenerInterface^ listener);
		void createFolder(String^ name, MNode^ parent);
		void moveNode(MNode^ node, MNode^ newParent, MRequestListenerInterface^ listener);
		void moveNode(MNode^ node, MNode^ newParent);
		void copyNode(MNode^ node, MNode^ newParent, MRequestListenerInterface^ listener);
		void copyNode(MNode^ node, MNode^ newParent);
		void renameNode(MNode^ node, String^ newName, MRequestListenerInterface^ listener);
		void renameNode(MNode^ node, String^ newName);
		void remove(MNode^ node, MRequestListenerInterface^ listener);
		void remove(MNode^ node);
		void shareWithUser(MNode^ node, MUser^ user, int level, MRequestListenerInterface^ listener);
		void shareWithUser(MNode^ node, MUser^ user, int level);
		void shareWithEmail(MNode^ node, String^ email, int level, MRequestListenerInterface^ listener);
		void shareWithEmail(MNode^ node, String^ email, int level);
		void folderAccess(String^ megaFolderLink, MRequestListenerInterface^ listener);
		void folderAccess(String^ megaFolderLink);
		void importFileLink(String^ megaFileLink, MNode^ parent, MRequestListenerInterface^ listener);
		void importFileLink(String^ megaFileLink, MNode^ parent);
		void importPublicNode(MNode^ publicNode, MNode^ parent, MRequestListenerInterface^ listener);
		void importPublicNode(MNode^ publicNode, MNode^ parent);
		void getPublicNode(String^ megaFileLink, MRequestListenerInterface^ listener);
		void getPublicNode(String^ megaFileLink);
		void getThumbnail(MNode^ node, String^ dstFilePath, MRequestListenerInterface^ listener);
		void getThumbnail(MNode^ node, String^ dstFilePath);
		void cancelGetThumbnail(MNode^ node, MRequestListenerInterface^ listener);
		void cancelGetThumbnail(MNode^ node);
		void setThumbnail(MNode^ node, String^ srcFilePath, MRequestListenerInterface^ listener);
		void setThumbnail(MNode^ node, String^ srcFilePath);
		void getPreview(MNode^ node, String^ dstFilePath, MRequestListenerInterface^ listener);
		void getPreview(MNode^ node, String^ dstFilePath);
		void cancelGetPreview(MNode^ node, MRequestListenerInterface^ listener);
		void cancelGetPreview(MNode^ node);
		void setPreview(MNode^ node, String^ srcFilePath, MRequestListenerInterface^ listener);
		void setPreview(MNode^ node, String^ srcFilePath);
		void getUserAvatar(MUser^ user, String^ dstFilePath, MRequestListenerInterface^ listener);
		void getUserAvatar(MUser^ user, String^ dstFilePath);
		void setAvatar(String ^dstFilePath, MRequestListenerInterface^ listener);
		void setAvatar(String ^dstFilePath);
		void exportNode(MNode^ node, MRequestListenerInterface^ listener);
		void exportNode(MNode^ node);
		void disableExport(MNode^ node, MRequestListenerInterface^ listener);
		void disableExport(MNode^ node);
		void fetchNodes(MRequestListenerInterface^ listener);
		void fetchNodes();
		void getAccountDetails(MRequestListenerInterface^ listener);
		void getAccountDetails();
		void getPricing(MRequestListenerInterface^ listener);
		void getPricing();
		void getPaymentUrl(uint64 productHandle, MRequestListenerInterface^ listener);
		void getPaymentUrl(uint64 productHandle);
		void changePassword(String^ oldPassword, String^ newPassword, MRequestListenerInterface^ listener);
		void changePassword(String^ oldPassword, String^ newPassword);
		void addContact(String^ email, MRequestListenerInterface^ listener);
		void addContact(String^ email);
		void removeContact(String^ email, MRequestListenerInterface^ listener);
		void removeContact(String^ email);
		void logout(MRequestListenerInterface^ listener);
		void logout();
		void startUpload(String^ localPath, MNode^ parent, MTransferListenerInterface^ listener);
		void startUpload(String^ localPath, MNode^ parent);
		void startUploadToFile(String^ localPath, MNode^ parent, String^ fileName, MTransferListenerInterface^ listener);
		void startUploadToFile(String^ localPath, MNode^ parent, String^ fileName);
		void startDownload(MNode^ node, String^ localPath, MTransferListenerInterface^ listener);
		void startDownload(MNode^ node, String^ localPath);
		void startPublicDownload(MNode^ node, String^ localPath, MTransferListenerInterface^ listener);
		void startPublicDownload(MNode^ node, String^ localPath);
		void startStreaming(MNode^ node, uint64 startPos, uint64 size, MTransferListenerInterface^ listener);
		void cancelTransfer(MTransfer^ transfer, MRequestListenerInterface^ listener);
		void cancelTransfer(MTransfer^ transfer);
		void cancelTransfers(int direction, MRequestListenerInterface^ listener);
		void cancelTransfers(int direction);
		void pauseTransfers(bool pause, MRequestListenerInterface^ listener);
		void pauseTransfers(bool pause);
		void submitFeedback(int rating, String^ comment, MRequestListenerInterface^ listener);
		void submitFeedback(int rating, String^ comment);
		void reportDebugEvent(String^ text, MRequestListenerInterface^ listener);
		void reportDebugEvent(String^ text);
		void setUploadLimit(int bpslimit);
		MTransferList^ getTransfers();
		int getNumPendingUploads();
		int getNumPendingDownloads();
		int getTotalUploads();
		int getTotalDownloads();
		uint64 getTotalDownloadedBytes();
		uint64 getTotalUploadedBytes();
		void resetTotalDownloads();
		void resetTotalUploads();
		int getNumChildren(MNode^ parent);
		int getNumChildFiles(MNode^ parent);
		int getNumChildFolders(MNode^ parent);
		MNodeList^ getChildren(MNode^ parent, int order);
		MNodeList^ getChildren(MNode^ parent);
		MNode^ getChildNode(MNode^ parent, String^ name);
		MNode^ getParentNode(MNode^ node);
		String^ getNodePath(MNode^ node);
		MNode^ getNodeByPath(String^ path, MNode^ n);
		MNode^ getNodeByPath(String^ path);
		MNode^ getNodeByHandle(uint64 handle);
		MUserList^ getContacts();
		MUser^ getContact(String^ email);
		MNodeList^ getInShares(MUser^ user);
		MNodeList^ getInShares();
		bool isShared(MNode^ node);
		MShareList^ getOutShares();
		MShareList^ getOutShares(MNode^ node);
		String^ getFileFingerprint(String^ filePath);
		String^ getNodeFingerprint(MNode^ node);
		MNode^ getNodeByFingerprint(String^ fingerprint);
		bool hasFingerprint(String^ fingerprint);
		int getAccess(MNode^ node);
		MError^ checkAccess(MNode^ node, int level);
		MError^ checkMove(MNode^ node, MNode^ target);
		MNode^ getRootNode();
		MNode^ getRubbishNode();
		MNodeList^ search(MNode^ node, String^ searchString, bool recursive);
		MNodeList^ search(MNode^ node, String^ searchString);
		bool processMegaTree(MNode^ node, MTreeProcessorInterface^ processor, bool recursive);
		bool processMegaTree(MNode^ node, MTreeProcessorInterface^ processor);

		//Logging
		static void setLogLevel(MLogLevel logLevel);
		static void setLoggerClass(MLoggerInterface^ megaLogger);
		static void log(MLogLevel logLevel, String^ message, String^ filename, int line);
		static void log(MLogLevel logLevel, String^ message, String^ filename);
		static void log(MLogLevel logLevel, String^ message);

	private:
		std::set<DelegateMRequestListener *> activeRequestListeners;
		std::set<DelegateMTransferListener *> activeTransferListeners;
		std::set<DelegateMGlobalListener *> activeGlobalListeners;
		std::set<DelegateMListener *> activeMegaListeners;
		CRITICAL_SECTION listenerMutex;

		MegaRequestListener *createDelegateMRequestListener(MRequestListenerInterface^ listener, bool singleListener = true);
		MegaTransferListener *createDelegateMTransferListener(MTransferListenerInterface^ listener, bool singleListener = true);
		MegaGlobalListener *createDelegateMGlobalListener(MGlobalListenerInterface^ listener);
		MegaListener *createDelegateMListener(MListenerInterface^ listener);
		MegaTreeProcessor *createDelegateMTreeProcessor(MTreeProcessorInterface^ processor);

		void freeRequestListener(DelegateMRequestListener *listener);
		void freeTransferListener(DelegateMTransferListener *listener);

		MegaApi *megaApi;
		DelegateMGfxProcessor *externalGfxProcessor;
		static DelegateMLogger* externalLogger;
		MegaApi *getCPtr();
	};
}
