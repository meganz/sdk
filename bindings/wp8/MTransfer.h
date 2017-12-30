/**
* @file MTransfer.h
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

#pragma once

#include "MNode.h"

#include "megaapi.h"

namespace mega
{
    using namespace Windows::Foundation;
    using Platform::String;

    public enum class MTransferType 
    { 
        TYPE_DOWNLOAD       = 0, 
        TYPE_UPLOAD         = 1 
    };

    public enum class MTransferState
    {
        STATE_NONE          = 0,
        STATE_QUEUED        = 1,
        STATE_ACTIVE        = 2,
        STATE_PAUSED        = 3,
        STATE_RETRYING      = 4,
        STATE_COMPLETING    = 5,
        STATE_COMPLETED     = 6,
        STATE_CANCELLED     = 7,
        STATE_FAILED        = 8
    };

    public ref class MTransfer sealed
    {
        friend ref class MegaSDK;
        friend ref class MTransferList;
        friend class DelegateMTransferListener;
        friend class DelegateMListener;

    public:
        virtual ~MTransfer();
        MTransfer^ copy();
        MTransferType getType();
        String^ getTransferString();
        String^ toString();
        uint64 getStartTime();
        uint64 getTransferredBytes();
        uint64 getTotalBytes();
        String^ getPath();
        String^ getParentPath();
        uint64 getNodeHandle();
        uint64 getParentHandle();
        uint64 getStartPos();
        uint64 getEndPos();
        String^ getFileName();
        int getNumRetry();
        int getMaxRetries();
        int getTag();
        uint64 getSpeed();
        uint64 getMeanSpeed();
        uint64 getDeltaSize();
        uint64 getUpdateTime();
        MNode^ getPublicMegaNode();
        bool isSyncTransfer();
        bool isStreamingTransfer();
        bool isFolderTransfer();
        int getFolderTransferTag();
        String^ getAppData();
        MTransferState getState();
        uint64 getPriority();
        uint64 getNotificationNumber();

    private:
        MTransfer(MegaTransfer *megaTransfer, bool cMemoryOwn);
        MegaTransfer *megaTransfer;
        MegaTransfer *getCPtr();
        bool cMemoryOwn;
    };
}
