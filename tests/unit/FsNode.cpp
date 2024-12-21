/**
 * (c) 2019 by Mega Limited, Wellsford, New Zealand
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

#include "FsNode.h"

#include "utils.h"

namespace mt {

using mega::FSACCESS_CLASS;
using mega::LocalPath;

//FsNode::FsNode(FsNode* parent, const mega::nodetype_t type, std::string name)
//: mFsId{mt::nextFsId()}
//, mMTime{nextRandomInt()}
//, mParent{parent}
//, mType{type}
//, mName{LocalPath::fromPlatformEncodedRelative(std::move(name))}
//{
//    assert(mType == mega::FILENODE || mType == mega::FOLDERNODE);
//
//    if (parent)
//    {
//        assert(parent->getType() == mega::FOLDERNODE);
//        parent->mChildren.push_back(this);
//    }
//
//    auto path = getPath();
//
//    if (mType == mega::FILENODE)
//    {
//        mSize = nextRandomInt();
//        mContent.reserve(static_cast<size_t>(mSize));
//        for (m_off_t i = 0; i < mSize; ++i)
//        {
//            mContent.push_back(nextRandomByte());
//        }
//        mFileAccess->fopen(path, true, false);
//        mFingerprint.genfingerprint(mFileAccess.get());
//    }
//    else
//    {
//        mFileAccess->fopen(path, true, false);
//        mFingerprint.mtime = mMTime;
//    }
//}
//
//FsNode::FileAccess::FileAccess(const FsNode& fsNode)
//: mFsNode{fsNode}
//{}
//
//bool FsNode::FileAccess::fopen(const LocalPath& path, bool, bool, mega::DirAccess* iteratingDir, bool)
//{
//    mPath = path;
//    return sysopen();
//}
//
//bool FsNode::FileAccess::sysstat(mega::m_time_t* curr_mtime, m_off_t* curr_size)
//{
//    *curr_mtime = mtime;
//    *curr_size = size;
//    return true;
//}
//
//bool FsNode::FileAccess::sysopen(bool async)
//{
//    if (mPath == mFsNode.getPath())
//    {
//        fsidvalid = true;
//        fsid = mFsNode.getFsId();
//        size = mFsNode.getSize();
//        mtime = mFsNode.getMTime();
//        type = mFsNode.getType();
//        return true;
//    }
//    else
//    {
//        return false;
//    }
//}
//
//bool FsNode::FileAccess::sysread(mega::byte* buffer, unsigned size, m_off_t offset)
//{
//    const auto& content = mFsNode.getContent();
//    assert(static_cast<unsigned>(offset) + size <= content.size());
//    std::copy(content.begin() + static_cast<unsigned>(offset), content.begin() + static_cast<unsigned>(offset) + size, buffer);
//    return true;
//}
//
//void FsNode::FileAccess::sysclose()
//{}

} // mt
