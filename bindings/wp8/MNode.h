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
		TYPE_RUBBISH,
		TYPE_MAIL
	};

	public ref class MNode sealed
	{
		friend ref class MegaSDK;
		friend ref class MNodeList;
		friend ref class MTransfer;
		friend ref class MRequest;

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
		int getTag();
		bool isFile();
		bool isFolder();
		bool isRemoved();
		bool isSyncDeleted();
		bool hasThumbnail();
		bool hasPreview();

	private:
		MNode(MegaNode *megaNode, bool cMemoryOwn);
		MegaNode *megaNode;
		MegaNode *getCPtr();
		bool cMemoryOwn;
	};
}
