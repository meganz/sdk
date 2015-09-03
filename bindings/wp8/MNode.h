/**
* @file MNode.h
* @brief Represent a node (file/folder) in the MEGA account
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

#include <megaapi.h>

namespace mega
{
	using namespace Windows::Foundation;
	using Platform::String;

	public enum class MNodeType {
		TYPE_UNKNOWN = -1,
		TYPE_FILE = 0,
		TYPE_FOLDER,
		TYPE_ROOT,
		TYPE_INCOMING,
		TYPE_RUBBISH
	};

    public enum class MNodeChangeType {
        CHANGE_TYPE_REMOVED         = 0x01,
        CHANGE_TYPE_ATTRIBUTES      = 0x02,
        CHANGE_TYPE_OWNER           = 0x04,
        CHANGE_TYPE_TIMESTAMP       = 0x08,
        CHANGE_TYPE_FILE_ATTRIBUTES = 0x10,
        CHANGE_TYPE_INSHARE         = 0x20,
        CHANGE_TYPE_OUTSHARE        = 0x40,
        CHANGE_TYPE_PARENT          = 0x80,
        CHANGE_TYPE_PENDINGSHARE    = 0x100
    };

	public ref class MNode sealed
	{
		friend ref class MegaSDK;
		friend ref class MNodeList;
		friend ref class MTransfer;
		friend ref class MRequest;
		friend class DelegateMTreeProcessor;

	public:
		virtual ~MNode();
		MNode^ copy();
		MNodeType getType();
		String^ getName();
		String^ getBase64Handle();
		uint64 getSize();
		uint64 getCreationTime();
		uint64 getModificationTime();
		uint64 getHandle();
        uint64 getParentHandle();
        String^ getBase64Key();
		int getTag();
		bool isFile();
		bool isFolder();
		bool isRemoved();
        bool hasChanged(int changeType);
        int getChanges();
		bool hasThumbnail();
		bool hasPreview();
        bool isPublic();

	private:
		MNode(MegaNode *megaNode, bool cMemoryOwn);
		MegaNode *megaNode;
		MegaNode *getCPtr();
		bool cMemoryOwn;
	};
}
