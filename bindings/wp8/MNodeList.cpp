#include "MNodeList.h"

using namespace mega;
using namespace Platform;

MNodeList::MNodeList(NodeList *nodeList, bool cMemoryOwn)
{
	this->nodeList = nodeList;
	this->cMemoryOwn = cMemoryOwn;
}

MNodeList::~MNodeList()
{
	if (cMemoryOwn)
		delete nodeList;
}

MNode^ MNodeList::get(int i)
{
	return nodeList ? ref new MNode(nodeList->get(i)->copy(), true) : nullptr;
}

int MNodeList::size()
{
	return nodeList ? nodeList->size() : 0;
}
