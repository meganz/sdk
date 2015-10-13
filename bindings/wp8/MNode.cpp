/**
* @file MNode.cpp
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

#include "MNode.h"

using namespace mega;
using namespace Platform;

MNode::MNode(MegaNode *megaNode, bool cMemoryOwn)
{
    this->megaNode = megaNode;
    this->cMemoryOwn = cMemoryOwn;
}

MNode::~MNode()
{
    if (cMemoryOwn)
        delete megaNode;
}

MegaNode* MNode::getCPtr()
{
    return megaNode;
}

MNode^ MNode::copy()
{
    return megaNode ? ref new MNode(megaNode->copy(), true) : nullptr;
}

MNodeType MNode::getType()
{
    return (MNodeType) (megaNode ? megaNode->getType() : MegaNode::TYPE_UNKNOWN);
}

String^ MNode::getName()
{
    if (!megaNode) return nullptr;

    std::string utf16name;
    const char *utf8name = megaNode->getName();
    MegaApi::utf8ToUtf16(utf8name, &utf16name);

    return utf8name ? ref new String((wchar_t *)utf16name.data()) : nullptr;
}

String^ MNode::getBase64Handle()
{
    if (!megaNode) return nullptr;

    std::string utf16base64Handle;
    const char *utf8base64Handle = megaNode->getBase64Handle();
    if (!utf8base64Handle)
        return nullptr;

    MegaApi::utf8ToUtf16(utf8base64Handle, &utf16base64Handle);
    delete [] utf8base64Handle;

    return ref new String((wchar_t *)utf16base64Handle.data());
}

uint64 MNode::getSize()
{
    return megaNode ? megaNode->getSize() : 0;
}

uint64 MNode::getCreationTime()
{
    return megaNode ? megaNode->getCreationTime() : 0;
}

uint64 MNode::getModificationTime()
{
    return megaNode ? megaNode->getModificationTime() : 0;
}

uint64 MNode::getHandle()
{
    return megaNode ? megaNode->getHandle() : ::mega::INVALID_HANDLE;
}

uint64 MNode::getParentHandle()
{
    return megaNode ? megaNode->getParentHandle() : ::mega::INVALID_HANDLE;
}

String^ MNode::getBase64Key()
{
    if (!megaNode) return nullptr;

    std::string utf16base64key;
    const char *utf8base64key = megaNode->getBase64Key();
    MegaApi::utf8ToUtf16(utf8base64key, &utf16base64key);

    return utf8base64key ? ref new String((wchar_t *)utf16base64key.data()) : nullptr;
}

int MNode::getTag()
{
    return megaNode ? megaNode->getTag() : 0;
}

uint64 MNode::getExpirationTime()
{
    return megaNode ? megaNode->getExpirationTime() : 0;
}

MegaHandle MNode::getPublicHandle()
{
    return megaNode ? megaNode->getPublicHandle() : ::mega::INVALID_HANDLE;
}

MNode^ MNode::getPublicNode()
{    
    return megaNode ? ref new MNode(megaNode->getPublicNode(), true) : nullptr;
}

String^ MNode::getPublicLink()
{
    return megaNode ? ref new String((wchar_t *)megaNode->getPublicLink()) : nullptr;
}

bool MNode::isFile()
{
    return megaNode ? megaNode->isFile() : false;
}

bool MNode::isFolder()
{
    return megaNode ? megaNode->isFolder() : false;
}

bool MNode::isRemoved()
{
    return megaNode ? megaNode->isRemoved() : false;
}

bool MNode::hasChanged(int changeType)
{
    return megaNode ? megaNode->hasChanged(changeType) : false;
}

int MNode::getChanges()
{
    return megaNode ? megaNode->getChanges() : 0;
}

bool MNode::hasThumbnail()
{
    return megaNode ? megaNode->hasThumbnail() : false;
}

bool MNode::hasPreview()
{
    return megaNode ? megaNode->hasPreview() : false;
}

bool MNode::isPublic()
{
    return megaNode ? megaNode->isPublic() : false;
}

bool MNode::isExported()
{
    return megaNode ? megaNode->isExported() : false;
}

bool MNode::isExpired()
{
    return megaNode ? megaNode->isExpired() : false;
}

bool MNode::isTakenDown()
{
    return megaNode ? megaNode->isTakenDown() : false;
}
