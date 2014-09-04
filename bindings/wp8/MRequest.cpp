#include "MRequest.h"

using namespace mega;
using namespace Platform;

MRequest::MRequest(MegaRequest *megaRequest, bool cMemoryOwn)
{
	this->megaRequest = megaRequest;
	this->cMemoryOwn = cMemoryOwn;
}

MRequest::~MRequest()
{
	if (cMemoryOwn)
		delete megaRequest;
}

MegaRequest* MRequest::getCPtr()
{
	return megaRequest;
}

MRequest^ MRequest::copy()
{
	return megaRequest ? ref new MRequest(megaRequest->copy(), true) : nullptr;
}

MRequestType MRequest::getType()
{
	return (MRequestType) (megaRequest ? megaRequest->getType() : -1);
}

String^ MRequest::getRequestString()
{
	if (!megaRequest) return nullptr;

	std::string utf16request;
	const char *utf8request = megaRequest->getRequestString();
	MegaApi::utf8ToUtf16(utf8request, &utf16request);

	return ref new String((wchar_t *)utf16request.data());
}

String^ MRequest::toString()
{
	return getRequestString();
}

uint64 MRequest::getNodeHandle()
{
	return megaRequest ? megaRequest->getNodeHandle() : ::mega::INVALID_HANDLE;
}

String^ MRequest::getLink()
{
	if (!megaRequest) return nullptr;

	std::string utf16link;
	const char *utf8link = megaRequest->getLink();
	MegaApi::utf8ToUtf16(utf8link, &utf16link);

	return ref new String((wchar_t *)utf16link.data());
}

uint64 MRequest::getParentHandle()
{
	return megaRequest ? megaRequest->getParentHandle() : ::mega::INVALID_HANDLE;
}

String^ MRequest::getSessionKey()
{
	if (!megaRequest) return nullptr;

	std::string utf16session;
	const char *utf8session = megaRequest->getSessionKey();
	MegaApi::utf8ToUtf16(utf8session, &utf16session);

	return ref new String((wchar_t *)utf16session.data());
}

String^ MRequest::getName()
{
	if (!megaRequest) return nullptr;

	std::string utf16name;
	const char *utf8name = megaRequest->getName();
	MegaApi::utf8ToUtf16(utf8name, &utf16name);

	return ref new String((wchar_t *)utf16name.data());
}

String^ MRequest::getEmail()
{
	if (!megaRequest) return nullptr;

	std::string utf16email;
	const char *utf8email = megaRequest->getEmail();
	MegaApi::utf8ToUtf16(utf8email, &utf16email);

	return ref new String((wchar_t *)utf16email.data());
}

String^ MRequest::getPassword()
{
	if (!megaRequest) return nullptr;

	std::string utf16password;
	const char *utf8password = megaRequest->getPassword();
	MegaApi::utf8ToUtf16(utf8password, &utf16password);

	return ref new String((wchar_t *)utf16password.data());
}

String^ MRequest::getNewPassword()
{
	if (!megaRequest) return nullptr;

	std::string utf16password;
	const char *utf8password = megaRequest->getNewPassword();
	MegaApi::utf8ToUtf16(utf8password, &utf16password);

	return ref new String((wchar_t *)utf16password.data());
}

String^ MRequest::getPrivateKey()
{
	if (!megaRequest) return nullptr;

	std::string utf16privateKey;
	const char *utf8privateKey = megaRequest->getPrivateKey();
	MegaApi::utf8ToUtf16(utf8privateKey, &utf16privateKey);

	return ref new String((wchar_t *)utf16privateKey.data());
}

int MRequest::getAccess()
{
	return megaRequest ? megaRequest->getAccess() : -1;
}

String^ MRequest::getFile()
{
	if (!megaRequest) return nullptr;

	std::string utf16file;
	const char *utf8file = megaRequest->getFile();
	MegaApi::utf8ToUtf16(utf8file, &utf16file);

	return ref new String((wchar_t *)utf16file.data());
}

MNode^ MRequest::getPublicNode()
{
	return megaRequest && megaRequest->getPublicNode() ? ref new MNode(megaRequest->getPublicNode()->copy(), true) : nullptr;
}

int MRequest::getParamType()
{
	return megaRequest ? megaRequest->getParamType() : 0;
}

bool MRequest::getFlag()
{
	return megaRequest ? megaRequest->getFlag() : 0;
}

uint64 MRequest::getTransferredBytes()
{
	return megaRequest ? megaRequest->getTransferredBytes() : 0;
}

uint64 MRequest::getTotalBytes()
{
	return megaRequest ? megaRequest->getTotalBytes() : 0;
}

MAccountDetails^ MRequest::getMAccountDetails()
{
	return nullptr;
}
