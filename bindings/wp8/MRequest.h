#pragma once

#include "MNode.h"
#include "MAccountDetails.h"
#include "MPricing.h"

#include "megaapi.h"

namespace mega
{
	using namespace Windows::Foundation;
	using Platform::String;

	public enum class MRequestType
	{
		TYPE_LOGIN, TYPE_MKDIR, TYPE_MOVE, TYPE_COPY,
		TYPE_RENAME, TYPE_REMOVE, TYPE_SHARE,
		TYPE_FOLDER_ACCESS, TYPE_IMPORT_LINK, TYPE_IMPORT_NODE,
		TYPE_EXPORT, TYPE_FETCH_NODES, TYPE_ACCOUNT_DETAILS,
		TYPE_CHANGE_PW, TYPE_UPLOAD, TYPE_LOGOUT, TYPE_FAST_LOGIN,
		TYPE_GET_PUBLIC_NODE, TYPE_GET_ATTR_FILE,
		TYPE_SET_ATTR_FILE, TYPE_GET_ATTR_USER,
		TYPE_SET_ATTR_USER, TYPE_RETRY_PENDING_CONNECTIONS,
		TYPE_ADD_CONTACT, TYPE_REMOVE_CONTACT, TYPE_CREATE_ACCOUNT, TYPE_FAST_CREATE_ACCOUNT,
		TYPE_CONFIRM_ACCOUNT, TYPE_FAST_CONFIRM_ACCOUNT,
		TYPE_QUERY_SIGNUP_LINK, TYPE_ADD_SYNC, TYPE_REMOVE_SYNC,
		TYPE_REMOVE_SYNCS, TYPE_PAUSE_TRANSFERS,
		TYPE_CANCEL_TRANSFER, TYPE_CANCEL_TRANSFERS,
		TYPE_DELETE, TYPE_REPORT_EVENT, TYPE_CANCEL_ATTR_FILE,
		TYPE_GET_PRICING
	};

	public ref class MRequest sealed
	{
		friend ref class MegaSDK;
		friend class DelegateMRequestListener;
		friend class DelegateMListener;

	public:
		virtual ~MRequest();
		MRequest^ copy();
		MRequestType getType();
		String^ getRequestString();
		String^ toString();
		uint64 getNodeHandle();
		String^ getLink();
		uint64 getParentHandle();
		String^ getSessionKey();
		String^ getName();
		String^ getEmail();
		String^ getPassword();
		String^ getNewPassword();
		String^ getPrivateKey();
		int getAccess();
		String^ getFile();
		MNode^ getPublicNode();
		int getParamType();
		bool getFlag();
		uint64 getTransferredBytes();
		uint64 getTotalBytes();
		MAccountDetails^ getMAccountDetails();
		MPricing^ getPricing();

	private:
		MRequest(MegaRequest *megaRequest, bool cMemoryOwn);
		MegaRequest *megaRequest;
		MegaRequest *getCPtr();
		bool cMemoryOwn;
	};
}

