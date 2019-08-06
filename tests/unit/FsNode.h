#pragma once

#include <map>
#include <memory>

#include <mega/types.h>
#include <mega/filefingerprint.h>

#include "DefaultedFileAccess.h"
#include "utils.h"

namespace mt {

// Represents a node on the filesystem (either file or directory)
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

    MEGA_DISABLE_COPY_MOVE(FsNode)

    void setFsId(const mega::handle fsId)
    {
        mFsId = fsId;
    }

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

    void setOpenable(const bool openable)
    {
        mOpenable = openable;
    }

    bool getOpenable() const
    {
        return mOpenable;
    }

    void setReadable(const bool readable)
    {
        mReadable = readable;
    }

    bool getReadable() const
    {
        return mReadable;
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
        {}

        bool fopen(std::string* path, bool, bool) override
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

        bool frawread(mega::byte* buffer, unsigned size, m_off_t offset) override
        {
            const auto& content = mFsNode.getContent();
            assert(static_cast<unsigned>(offset) + size <= content.size());
            std::copy(content.begin() + offset, content.begin() + offset + size, buffer);
            return true;
        }

    private:
        const FsNode& mFsNode;
    };

    mega::handle mFsId = mega::UNDEF;
    m_off_t mSize = -1;
    mega::m_time_t mMTime = 0;
    std::vector<mega::byte> mContent;
    mega::FileFingerprint mFingerprint;
    std::unique_ptr<mega::FileAccess> mFileAccess = std::unique_ptr<mega::FileAccess>{new FileAccess{*this}};
    const FsNode* mParent = nullptr;
    const mega::nodetype_t mType = mega::TYPE_UNKNOWN;
    const std::string mName;
    bool mOpenable = true;
    bool mReadable = true;
    std::vector<const FsNode*> mChildren;
};

} // mt
