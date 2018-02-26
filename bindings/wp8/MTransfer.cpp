/**
* @file MTransfer.cpp
* @brief Provides information about a transfer
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

int MTransfer::getTag()
{
    return megaTransfer ? megaTransfer->getTag() : 0;
}

uint64 MTransfer::getSpeed()
{
    return megaTransfer ? megaTransfer->getSpeed() : 0;
}

uint64 MTransfer::getMeanSpeed()
{
    return megaTransfer ? megaTransfer->getMeanSpeed() : 0;
}

uint64 MTransfer::getDeltaSize()
{
    return megaTransfer ? megaTransfer->getDeltaSize() : 0;
}

uint64 MTransfer::getUpdateTime()
{
    return megaTransfer ? megaTransfer->getUpdateTime() : 0;
}

MNode^ MTransfer::getPublicMegaNode()
{
    if (!megaTransfer) return nullptr;
    
    MegaNode *node = megaTransfer->getPublicMegaNode();
    return node ? ref new MNode(node, true) : nullptr;
}

bool MTransfer::isSyncTransfer()
{
    return megaTransfer ? megaTransfer->isSyncTransfer() : false;
}

bool MTransfer::isStreamingTransfer()
{
    return megaTransfer ? megaTransfer->isStreamingTransfer() : false;
}

bool MTransfer::isFolderTransfer()
{
    return megaTransfer ? megaTransfer->isFolderTransfer() : false;
}

int MTransfer::getFolderTransferTag()
{
    return megaTransfer ? megaTransfer->getFolderTransferTag() : 0;
}

String^ MTransfer::getAppData()
{
    if (!megaTransfer) return nullptr;

    std::string utf16appData;
    const char *utf8appData = megaTransfer->getAppData();
    MegaApi::utf8ToUtf16(utf8appData, &utf16appData);

    return ref new String((wchar_t *)utf16appData.data());
}

MTransferState MTransfer::getState()
{
    return (MTransferState) (megaTransfer ? megaTransfer->getState() : 0);
}

uint64 MTransfer::getPriority()
{
    return megaTransfer ? megaTransfer->getPriority() : 0;
}

uint64 MTransfer::getNotificationNumber()
{
    return megaTransfer ? megaTransfer->getNotificationNumber() : 0;
}
