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

#pragma once

#include <map>
#include <memory>

#include <mega/types.h>
#include <mega/filefingerprint.h>

#include "DefaultedFileAccess.h"

namespace mt {

// Represents a node on the filesystem (either file or directory)
class FsNode
{
public:
    FsNode(FsNode* parent, const mega::nodetype_t type, std::string name);

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

    void assignContentFrom(const FsNode& node)
    {
        assert(node.getType() == mega::FILENODE);
        mSize = node.getSize();
        mMTime = node.getMTime();
        mContent = node.getContent();
        mFingerprint = node.getFingerprint();
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
        explicit FileAccess(const FsNode& fsNode);

        bool fopen(std::string* path, bool, bool) override;

        bool frawread(mega::byte* buffer, unsigned size, m_off_t offset) override;

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
