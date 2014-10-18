#include "DelegateMTreeProcessor.h"

using namespace mega;

DelegateMTreeProcessor::DelegateMTreeProcessor(MTreeProcessorInterface^ processor)
{
	this->processor = processor;
}

bool DelegateMTreeProcessor::processMegaNode(MegaNode* node)
{
	if (processor != nullptr)
		return processor->processMNode(ref new MNode(node, false));

	return false;
}
