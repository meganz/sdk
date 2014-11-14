#include "MTransfer.h"

using namespace mega;
using namespace Platform;

MTransfer::MTransfer(MegaTransfer *megaTransfer, bool cMemoryOwn)
{
	this->megaTransfer = megaTransfer;
	this->cMemoryOwn = cMemoryOwn;
}

MTransfer::~MTransfer()
{
	if (cMemoryOwn)
		delete megaTransfer;
}

MegaTransfer *MTransfer::getCPtr()
{
	return megaTransfer;
}

MTransfer^ MTransfer::copy()
{
	return megaTransfer ? ref new MTransfer(megaTransfer->copy(), true) : nullptr;
}

MTransferType MTransfer::getType()
{
	return (MTransferType) (megaTransfer ? megaTransfer->getType() : 0);
}

String^ MTransfer::getTransferString()
{
	if (!megaTransfer) return nullptr;

	std::string utf16string;
	const char *utf8string = megaTransfer->getTransferString();
	MegaApi::utf8ToUtf16(utf8string, &utf16string);

	return ref new String((wchar_t *)utf16string.data());
}

String^ MTransfer::toString()
{
	return getTransferString();
}

uint64 MTransfer::getStartTime() 
{
	return megaTransfer ? megaTransfer->getStartTime() : 0;
}

uint64 MTransfer::getTransferredBytes()
{
	return megaTransfer ? megaTransfer->getTransferredBytes() : 0;
}

uint64 MTransfer::getTotalBytes()
{
	return megaTransfer ? megaTransfer->getTotalBytes() : 0;
}

String^ MTransfer::getPath()
{
	if (!megaTransfer) return nullptr;

	std::string utf16path;
	const char *utf8path = megaTransfer->getPath();
	MegaApi::utf8ToUtf16(utf8path, &utf16path);

	return ref new String((wchar_t *)utf16path.data());
}

String^ MTransfer::getParentPath()
{
	if (!megaTransfer) return nullptr;

	std::string utf16path;
	const char *utf8path = megaTransfer->getParentPath();
	MegaApi::utf8ToUtf16(utf8path, &utf16path);

	return ref new String((wchar_t *)utf16path.data());
}

uint64 MTransfer::getNodeHandle()
{
	return megaTransfer ? megaTransfer->getNodeHandle() : ::mega::INVALID_HANDLE;
}

uint64 MTransfer::getParentHandle()
{
	return megaTransfer ? megaTransfer->getParentHandle() : ::mega::INVALID_HANDLE;
}

uint64 MTransfer::getStartPos() 
{
	return megaTransfer ? megaTransfer->getStartPos() : 0;
}

uint64 MTransfer::getEndPos()
{
	return megaTransfer ? megaTransfer->getEndPos() : 0;
}

String^ MTransfer::getFileName() 
{
	if (!megaTransfer) return nullptr;

	std::string utf16name;
	const char *utf8name = megaTransfer->getFileName();
	MegaApi::utf8ToUtf16(utf8name, &utf16name);

	return ref new String((wchar_t *)utf16name.data());
}

int MTransfer::getNumRetry()
{
	return megaTransfer ? megaTransfer->getNumRetry() : 0;
}

int MTransfer::getMaxRetries()
{
	return megaTransfer ? megaTransfer->getMaxRetries() : 0;
}

uint64 MTransfer::getTime()
{
	return megaTransfer ? megaTransfer->getTime() : 0;
}

String^ MTransfer::getBase64Key()
{
	if (!megaTransfer) return nullptr;

	std::string utf16base64key;
	const char *utf8base64key = megaTransfer->getBase64Key();
	MegaApi::utf8ToUtf16(utf8base64key, &utf16base64key);

	return ref new String((wchar_t *)utf16base64key.data());
}

int MTransfer::getTag()
{
	return megaTransfer ? megaTransfer->getTag() : 0;
}

uint64 MTransfer::getSpeed()
{
	return megaTransfer ? megaTransfer->getSpeed() : 0;
}

uint64 MTransfer::getDeltaSize()
{
	return megaTransfer ? megaTransfer->getDeltaSize() : 0;
}

uint64 MTransfer::getUpdateTime()
{
	return megaTransfer ? megaTransfer->getUpdateTime() : 0;
}

MNode^ MTransfer::getPublicNode()
{
	return megaTransfer && megaTransfer->getPublicNode() ? ref new MNode(megaTransfer->getPublicMegaNode(), true) : nullptr;
}

bool MTransfer::isSyncTransfer()
{
	return megaTransfer ? megaTransfer->isSyncTransfer() : false;
}

bool MTransfer::isStreamingTransfer()
{
	return megaTransfer ? megaTransfer->isStreamingTransfer() : false;
}

