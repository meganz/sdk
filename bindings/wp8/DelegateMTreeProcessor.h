#pragma once

#include "MegaSDK.h"
#include "MTreeProcessorInterface.h"

#include "megaapi.h"

namespace mega
{
	ref class MegaSDK;

	using namespace Windows::Foundation;
	using Platform::String;

	private class DelegateMTreeProcessor : public MegaTreeProcessor
	{
	public:
		DelegateMTreeProcessor(MTreeProcessorInterface^ listener);
		virtual bool processMegaNode(MegaNode* node);

	private:
		MTreeProcessorInterface^ processor;
	};
}
