#pragma once

#include "MNode.h"

#include "megaapi.h"

namespace mega
{
	using namespace Windows::Foundation;
	using Platform::String;

	public enum class MTransferType { TYPE_DOWNLOAD, TYPE_UPLOAD };

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
		uint64 getDeltaSize();
		uint64 getUpdateTime();
		MNode^ getPublicMegaNode();
		bool isSyncTransfer();
		bool isStreamingTransfer();

	private:
		MTransfer(MegaTransfer *megaTransfer, bool cMemoryOwn);
		MegaTransfer *megaTransfer;
		MegaTransfer *getCPtr();
		bool cMemoryOwn;
	};
}
