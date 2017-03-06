/**
* @file MTransferData.cpp
* @brief Provides information about transfer queues
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

#include "MTransferData.h"

using namespace mega;
using namespace Platform;

MTransferData::MTransferData(MegaTransferData *megaTransferData, bool cMemoryOwn)
{
    this->megaTransferData = megaTransferData;
    this->cMemoryOwn = cMemoryOwn;
}

MTransferData::~MTransferData()
{
    if (cMemoryOwn)
        delete megaTransferData;
}

MegaTransferData * MTransferData::getCPtr()
{
    return megaTransferData;
}

MTransferData^ MTransferData::copy()
{
    return megaTransferData ? ref new MTransferData(megaTransferData->copy(), true) : nullptr;
}

int MTransferData::getNumDownloads()
{
    return megaTransferData ? megaTransferData->getNumDownloads() : 0;
}

int MTransferData::getNumUploads()
{
    return megaTransferData ? megaTransferData->getNumUploads() : 0;
}

int MTransferData::getDownloadTag(int i)
{
    return megaTransferData ? megaTransferData->getDownloadTag(i) : 0;
}

int MTransferData::getUploadTag(int i)
{
    return megaTransferData ? megaTransferData->getUploadTag(i) : 0;
}

unsigned long long MTransferData::getDownloadPriority(int i)
{
    return megaTransferData ? megaTransferData->getDownloadPriority(i) : 0;
}

unsigned long long MTransferData::getUploadPriority(int i)
{
    return megaTransferData ? megaTransferData->getUploadPriority(i) : 0;
}

long long MTransferData::getNotificationNumber()
{
    return megaTransferData ? megaTransferData->getNotificationNumber() : 0;
}
