#pragma once

#include "MNode.h"

namespace mega
{
	using namespace Windows::Foundation;
	using Platform::String;

	public interface class MTreeProcessorInterface
	{
	public:
		bool processMNode(MNode^ node);
	};
}
