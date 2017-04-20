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

String^ MNode::getFingerprint()
{
    if (!megaNode) return nullptr;

    std::string utf16fingerprint;
    const char *utf8fingerprint = megaNode->getFingerprint();
    MegaApi::utf8ToUtf16(utf8fingerprint, &utf16fingerprint);

    return utf8fingerprint ? ref new String((wchar_t *)utf16fingerprint.data()) : nullptr;
}

bool MNode::hasCustomAttrs()
{
    return megaNode ? megaNode->hasCustomAttrs() : false;
}

MStringList^ MNode::getCustomAttrNames()
{
    return megaNode ? ref new MStringList(megaNode->getCustomAttrNames(), true) : nullptr;
}

String^ MNode::getCustomAttr(String^ attrName)
{
    if (!megaNode || attrName == nullptr) return nullptr;

    std::string utf8attrName;
    MegaApi::utf16ToUtf8(attrName->Data(), attrName->Length(), &utf8attrName);

    const char *utf8customAttr = megaNode->getCustomAttr(utf8attrName.c_str());
    if (!utf8customAttr)
        return nullptr;

    std::string utf16customAttr;
    MegaApi::utf8ToUtf16(utf8customAttr, &utf16customAttr);
    delete[] utf8customAttr;

    return ref new String((wchar_t *)utf16customAttr.c_str());
}

int MNode::getDuration()
{
    return megaNode ? megaNode->getDuration() : MegaNode::INVALID_DURATION;
}

double MNode::getLatitude()
{
    return megaNode ? megaNode->getLatitude() : MegaNode::INVALID_COORDINATE;
}

double MNode::getLongitude()
{
    return megaNode ? megaNode->getLongitude() : MegaNode::INVALID_COORDINATE;
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
    if (!utf8base64key)
        return nullptr;

    MegaApi::utf8ToUtf16(utf8base64key, &utf16base64key);
    delete[] utf8base64key;

    return ref new String((wchar_t *)utf16base64key.data());
}

int MNode::getTag()
{
    return megaNode ? megaNode->getTag() : 0;
}

int64 MNode::getExpirationTime()
{
    return megaNode ? megaNode->getExpirationTime() : -1;
}

MegaHandle MNode::getPublicHandle()
{
    return megaNode ? megaNode->getPublicHandle() : ::mega::INVALID_HANDLE;
}

MNode^ MNode::getPublicNode()
{    
    return megaNode ? ref new MNode(megaNode->getPublicNode(), true) : nullptr;
}

String^ MNode::getPublicLink(bool includeKey)
{
    if (!megaNode) return nullptr;

    std::string utf16link;
    const char *utf8link = megaNode->getPublicLink(includeKey);
    if (!utf8link)
        return nullptr;

    MegaApi::utf8ToUtf16(utf8link, &utf16link);
    delete[] utf8link;

    return ref new String((wchar_t *)utf16link.data());
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

bool MNode::isForeign()
{
    return megaNode ? megaNode->isForeign() : false;
}

bool MNode::isShared()
{
    return megaNode ? megaNode->isShared() : false;
}

bool MNode::isOutShare()
{
    return megaNode ? megaNode->isOutShare() : false;
}

bool MNode::isInShare()
{
    return megaNode ? megaNode->isInShare() : false;
}
