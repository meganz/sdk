#pragma once

#include "MNode.h"

#include "megaapi.h"

namespace mega
{
	using namespace Windows::Foundation;
	using Platform::String;

	public ref class MNodeList sealed
	{
		friend ref class MegaSDK;

	public:
		virtual ~MNodeList();
		MNode^ get(int i);
		int size();

	private:
		MNodeList(MegaNodeList *nodeList, bool cMemoryOwn);
		MegaNodeList *nodeList;
		bool cMemoryOwn;
	};
}
