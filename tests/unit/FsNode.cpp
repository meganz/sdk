#include "FsNode.h"

namespace mt {

FsNode::FsNode(FsNode* parent, const mega::nodetype_t type, std::string name)
: mFsId{mt::nextFsId()}
, mMTime{nextRandomInt()}
, mParent{parent}
, mType{type}
, mName{std::move(name)}
{
    assert(mType == mega::FILENODE || mType == mega::FOLDERNODE);

    if (parent)
    {
        parent->mChildren.push_back(this);
    }

    auto path = getPath();

    if (mType == mega::FILENODE)
    {
        mSize = nextRandomInt();
        mContent.reserve(mSize);
        for (m_off_t i = 0; i < mSize; ++i)
        {
            mContent.push_back(nextRandomByte());
        }
        mFileAccess->fopen(&path, true, false);
        mFingerprint.genfingerprint(mFileAccess.get());
    }
    else
    {
        mFileAccess->fopen(&path, true, false);
        mFingerprint.isvalid = true;
        mFingerprint.mtime = mMTime;
    }
}

FsNode::FileAccess::FileAccess(const FsNode& fsNode)
: mFsNode{fsNode}
{}

bool FsNode::FileAccess::fopen(std::string* path, bool, bool)
{
    if (*path == mFsNode.getPath())
    {
        fsidvalid = true;
        fsid = mFsNode.getFsId();
        size = mFsNode.getSize();
        mtime = mFsNode.getMTime();
        type = mFsNode.getType();
        return true;
    }
    else
    {
        return false;
    }
}

bool FsNode::FileAccess::frawread(mega::byte* buffer, unsigned size, m_off_t offset)
{
    const auto& content = mFsNode.getContent();
    assert(static_cast<unsigned>(offset) + size <= content.size());
    std::copy(content.begin() + offset, content.begin() + offset + size, buffer);
    return true;
}

} // mt
