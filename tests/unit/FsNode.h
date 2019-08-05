#pragma once

#include <map>
#include <memory>

#include <mega/types.h>
#include <mega/filefingerprint.h>

#include "DefaultedFileAccess.h"
#include "utils.h"

namespace mt {

class FsNode
{
public:
    FsNode(FsNode* parent, const mega::nodetype_t type, std::string name)
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
        if (mType == mega::FILENODE)
        {
            mSize = nextRandomInt();
            for (m_off_t i = 0; i < mSize; ++i)
            {
                mContent.push_back(nextRandomByte());
            }
            FileAccess fa{*this};
            mFingerprint.genfingerprint(&fa);
        }
        else
        {
            mFingerprint.isvalid = true;
            mFingerprint.mtime = mMTime;
        }
    }

    MEGA_DISABLE_COPY_MOVE(FsNode)

    mega::handle getFsId() const
    {
        return mFsId;
    }

    m_off_t getSize() const
    {
        return mSize;
    }

    mega::m_time_t getMTime() const
    {
        return mMTime;
    }

    const std::vector<mega::byte>& getContent() const
    {
        return mContent;
    }

    const mega::FileFingerprint& getFingerprint() const
    {
        return mFingerprint;
    }

    mega::nodetype_t getType() const
    {
        return mType;
    }

    const std::string& getName() const
    {
        return mName;
    }

    std::string getPath() const
    {
        std::string path = mName;
        auto parent = mParent;
        while (parent)
        {
            path = parent->mName + "/" + path;
            parent = parent->mParent;
        }
        return path;
    }

    const std::vector<const FsNode*>& getChildren() const
    {
        return mChildren;
    }

private:

    class FileAccess : public DefaultedFileAccess
    {
    public:
        explicit FileAccess(const FsNode& fsNode)
        : mFsNode{fsNode}
        {
            fsidvalid = true;
            fsid = mFsNode.getFsId();
            size = mFsNode.getSize();
            mtime = mFsNode.getMTime();
            type = mFsNode.getType();
        }

        bool frawread(mega::byte* buffer, unsigned size, m_off_t) override
        {
            for (unsigned i = 0; i < size; ++i)
            {
                assert(i < mFsNode.getContent().size());
                buffer[i] = mFsNode.getContent()[i];
            }
            return true;
        }

    private:
        const FsNode& mFsNode;
    };

    mega::handle mFsId;
    m_off_t mSize;
    mega::m_time_t mMTime;
    std::vector<mega::byte> mContent;
    mega::FileFingerprint mFingerprint;
    const FsNode* mParent;
    const mega::nodetype_t mType;
    const std::string mName;
    std::vector<const FsNode*> mChildren;
};

} // mt
