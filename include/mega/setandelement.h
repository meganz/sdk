/**
* @file mega/setandelement.h
* @brief Class for manipulating Sets and their Elements
*
* (c) 2013-2022 by Mega Limited, Wellsford, New Zealand
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

#ifndef MEGA_SET_AND_ELEMENT_H
#define MEGA_SET_AND_ELEMENT_H 1

#include "types.h"

#include <bitset>
#include <cassert>
#include <functional>
#include <memory>
#include <string>

namespace mega {

    /**
     * @brief Base class for common characteristics of Set and Element
     */
    class CommonSE
    {
    public:
        // get own id
        const handle& id() const { return mId; }

        // get key used for encrypting attrs
        // SETNODEKEYLENGTH at types.h (128 = SymmCipher::KEYLENGTH)
        const std::string& key() const { return mKey; }

        // get timestamp
        const m_time_t& ts() const { return mTs; }

        // get own name
        const std::string& name() const { return getAttr(nameTag); }

        // set own id
        void setId(handle id) { mId = id; }

        // set key used for encrypting attrs
        void setKey(const std::string& key) { mKey = key; }
        void setKey(std::string&& key) { mKey = std::move(key); }

        // set timestamp
        void setTs(m_time_t ts) { mTs = ts; }

        // set own name
        void setName(std::string&& name);

        // test for attrs (including empty "" ones)
        bool hasAttrs() const { return !!mAttrs; }

        // test for encrypted attrs, that will need a call to decryptAttributes()
        bool hasEncrAttrs() const { return !!mEncryptedAttrs; }

        // set encrypted attrs, that will need a call to decryptAttributes()
        void setEncryptedAttrs(std::string&& eattrs) { mEncryptedAttrs.reset(new std::string(std::move(eattrs))); }

        // decrypt attributes set with setEncryptedAttrs(), and replace internal attrs
        bool decryptAttributes(std::function<bool(const std::string&, const std::string&, string_map&)> f);

        // encrypt internal attrs and return the result
        std::string encryptAttributes(std::function<std::string(const string_map&, const std::string&)> f) const;

        static const int HANDLESIZE = 8;
        static const int PUBLICHANDLESIZE = 6;

    protected:
        CommonSE() = default;
        CommonSE(handle id, std::string&& key, string_map&& attrs) : mId(id), mKey(std::move(key)), mAttrs(new string_map(std::move(attrs))) {}
        CommonSE(const CommonSE& src) { replaceCurrent(src); }
        CommonSE& operator=(const CommonSE& src) { replaceCurrent(src); return *this; }
        CommonSE(CommonSE&&) = default;
        CommonSE& operator=(CommonSE&&) = default;
        ~CommonSE() = default;

        handle mId = UNDEF;
        std::string mKey;
        std::unique_ptr<string_map> mAttrs;
        m_time_t mTs = 0;  // timestamp

        void setAttr(const std::string& tag, std::string&& value); // set any non-standard attr
        const std::string& getAttr(const std::string& tag) const;
        bool hasAttrChanged(const std::string& tag, const std::unique_ptr<string_map>& otherAttrs) const;
        void rebaseCommonAttrsOn(const string_map* baseAttrs);

        static bool validChangeType(const uint64_t& typ, const uint64_t& typMax) { assert(typ < typMax); return typ < typMax; }

        std::unique_ptr<std::string> mEncryptedAttrs;             // "at": up to 65535 bytes of miscellaneous data, encrypted with mKey

        static const std::string nameTag; // "n", used for 'name' attribute

    private:
        void replaceCurrent(const CommonSE& src)
        {
            this->mId = src.mId;
            this->mKey = src.mKey;
            this->mAttrs.reset(src.mAttrs ? new string_map(*src.mAttrs) : nullptr);
            this->mTs = src.mTs;
            this->mEncryptedAttrs.reset(src.mEncryptedAttrs ? new std::string(*src.mEncryptedAttrs) : nullptr);
        }
    };

    /**
     * @brief Internal representation of an Element
     */
    class SetElement : public CommonSE, public Cacheable
    {
    public:
        SetElement() = default;
        SetElement(handle sid, handle node, handle elemId, std::string&& key, string_map&& attrs)
            : CommonSE(elemId, std::move(key), std::move(attrs)), mSetId(sid), mNodeHandle(node) {}
        SetElement(const SetElement& src) : CommonSE(src) { replaceCurrent(src); }
        SetElement& operator=(const SetElement& src) { CommonSE::operator=(src); replaceCurrent(src); return *this; }
        SetElement(SetElement&&) = default;
        SetElement& operator=(SetElement&&) = default;
        ~SetElement() = default;

        // return id of the set that owns this Element
        const handle& set() const { return mSetId; }

        // return handle of the node represented by this Element
        const handle& node() const { return mNodeHandle; }

        // return order of this Element
        int64_t order() const { return mOrder ? *mOrder : 0; }

        // set id of the set that owns this Element
        void setSet(handle s) { mSetId = s; }

        // set handle of the node represented by this Element
        void setNode(handle nh) { mNodeHandle = nh; mNodeMetadata.reset(); }

        // set order of this Element
        void setOrder(int64_t order);

        // return true if last change modified order of this Element
        // (useful for instances that only contain updates)
        bool hasOrder() const { return !!mOrder; }

        // replace internal parameters with the ones of 'el', and mark any CH_EL_XXX change
        bool updateWith(SetElement&& el);

        // apply attrs on top of the ones of 'el' (useful for instances that only contain updates)
        void rebaseAttrsOn(const SetElement& el) { rebaseCommonAttrsOn(el.mAttrs.get()); }

        // mark attrs being cleared by the last update (reason for internal container being null)
        // (useful for instances that only contain updates)
        void setAttrsClearedByLastUpdate(bool cleared) { mAttrsClearedByLastUpdate = cleared; }

        // return true if attrs have been cleared in the last update (reason for internal container being null)
        // (useful for instances that only contain updates)
        bool hasAttrsClearedByLastUpdate() const { return mAttrsClearedByLastUpdate; }

        // mark a change to internal parameters (useful for app notifications)
        void setChanged(int changeType);

        // reset changes of internal parameters (call after app has been notified)
        void resetChanges() { mChanges = 0; }

        // return changes to internal parameters (useful for app notifications)
        unsigned long changes() const { return mChanges.to_ulong(); }

        // return true if internal parameter pointed out by changeType has changed (useful for app
        // notifications)
        bool hasChanged(uint64_t changeType) const
        {
            return validChangeType(changeType, CH_EL_SIZE) ?
                       mChanges[static_cast<size_t>(changeType)] :
                       false;
        }

        bool serialize(std::string*) const override;
        static std::unique_ptr<SetElement> unserialize(std::string* d);

        enum // match MegaSetElement::CHANGE_TYPE_ELEM_XXX values
        {
            CH_EL_NEW,      // point out that this is a new Element
            CH_EL_NAME,     // point out that 'name' attr has changed
            CH_EL_ORDER,    // point out that order has changed
            CH_EL_REMOVED,  // point out that this Element has been removed

            CH_EL_SIZE
        };

        struct NodeMetadata
        {
            handle h = UNDEF; // node handle
            handle u = UNDEF; // owning user
            m_off_t s = 0; // size
            string at; // node attributes
            string fingerprint;
            string filename;
            string fa; // file attributes
            m_time_t ts; // timestamp
        };

        // return node metadata in case of Element in preview, null otherwise
        const NodeMetadata* nodeMetadata() const { return mNodeMetadata.get(); }

        void setNodeMetadata(NodeMetadata&& nm)
        {
            assert(mNodeHandle == nm.h);
            mNodeMetadata.reset(new NodeMetadata(std::move(nm)));
        }

    private:
        handle mSetId = UNDEF;
        handle mNodeHandle = UNDEF;
        std::unique_ptr<NodeMetadata> mNodeMetadata;
        std::unique_ptr<int64_t> mOrder;
        bool mAttrsClearedByLastUpdate = false;

        std::bitset<CH_EL_SIZE> mChanges;

        void replaceCurrent(const SetElement& src)
        {
            this->mSetId = src.mSetId;
            this->mNodeHandle = src.mNodeHandle;
            this->mNodeMetadata.reset(src.mNodeMetadata ? new NodeMetadata(*src.mNodeMetadata) : nullptr);
            this->mOrder.reset(src.mOrder ? new int64_t(*src.mOrder) : nullptr);
            this->mAttrsClearedByLastUpdate = src.mAttrsClearedByLastUpdate;
            this->mChanges = src.mChanges;
        }
    };

    /**
     * @brief Internal representation of a public link from a set
     */
    class PublicLinkSet
    {
    public:
        enum class LinkDeletionReason : uint8_t
        {
            NO_REMOVED = 0,
            BY_USER,
            DISPUTE,
            ETD,
            ATD,
        };

        static constexpr uint64_t ETD_REMOVED_API_CODE = 4294967275; // Defined by API
        static constexpr uint64_t ATD_REMOVED_API_CODE = 4294967274; // Defined by API
        static constexpr uint64_t USER_REMOVED_API_CODE = 0; // Defined by API

        static LinkDeletionReason apiCodeToDeletionReason(const int64_t apiCode)
        {
            switch (apiCode)
            {
                case USER_REMOVED_API_CODE:
                    return LinkDeletionReason::BY_USER;
                case ETD_REMOVED_API_CODE:
                    return LinkDeletionReason::ETD;
                case ATD_REMOVED_API_CODE:
                    return LinkDeletionReason::ATD;
                default:
                    return LinkDeletionReason::DISPUTE;
            }
        }

        static std::string LinkDeletionReasonToString(const LinkDeletionReason reason)
        {
            switch (reason)
            {
                case LinkDeletionReason::NO_REMOVED:
                    return "not removed";
                case LinkDeletionReason::BY_USER:
                    return "by user";
                case LinkDeletionReason::DISPUTE:
                    return "dispute";
                case LinkDeletionReason::ETD:
                    return "ETD";
                case LinkDeletionReason::ATD:
                    return "ATD";
            }
            // Silent compilation warning
            return "";
        }

        PublicLinkSet(handle publicId):
            mPublicId(publicId)
        {}

        PublicLinkSet* copy() const
        {
            return new PublicLinkSet(*this);
        }

        // set public id of the set (Set exported)
        void setPublicId(handle pid)
        {
            mPublicId = pid;
        }

        // set if link has been taken down
        void setTakeDown(bool takedown)
        {
            mTakedown = takedown;
        }

        // set the reason for link removal
        void setLinkDeletionReason(LinkDeletionReason deletionReason)
        {
            mLinkDeletionReason = deletionReason;
        }

        // return public id of the set
        handle getPublicHandle() const
        {
            return mPublicId;
        }

        // returns true if link has been taken down
        bool isTakenDown() const
        {
            return mTakedown;
        }

        // returns the reason for link removal
        LinkDeletionReason getLinkDeletionReason() const
        {
            return mLinkDeletionReason;
        }

    protected:
        handle mPublicId{UNDEF};
        bool mTakedown{};
        LinkDeletionReason mLinkDeletionReason{LinkDeletionReason::NO_REMOVED};

        PublicLinkSet(const PublicLinkSet& publicLink):
            mPublicId(publicLink.mPublicId),
            mTakedown(publicLink.mTakedown),
            mLinkDeletionReason(publicLink.mLinkDeletionReason)
        {}
    };

    /**
     * @brief Internal representation of a Set
     */
    class Set: public CommonSE, public Cacheable
    {
    public:
        using SetType = uint8_t;

        Set() = default;

        Set(handle id,
            std::string&& key,
            handle user,
            string_map&& attrs,
            SetType type = TYPE_ALBUM):
            CommonSE(id, std::move(key), std::move(attrs)),
            mUser(user),
            mType(type)
        {}

        Set(const Set& s):
            CommonSE(s),
            mUser(s.mUser),
            mCTs(s.mCTs),
            mType(s.mType),
            mChanges(s.mChanges)
        {
            if (s.getPublicLink())
            {
                mPublicLink.reset(s.getPublicLink()->copy());
            }
        }

        Set(Set&& s):
            CommonSE(s),
            mUser(s.mUser),
            mCTs(s.mCTs),
            mType(s.mType),
            mChanges(s.mChanges),
            mPublicLink(std::move(s.mPublicLink))
        {}

        Set& operator=(const Set& src)
        {
            mId = src.mId;
            mKey = src.mKey;
            mAttrs.reset(src.mAttrs ? new string_map(*src.mAttrs) : nullptr);
            mTs = src.mTs;
            mEncryptedAttrs.reset(src.mEncryptedAttrs ? new std::string(*src.mEncryptedAttrs) :
                                                        nullptr);
            mUser = src.mUser;
            mCTs = src.mCTs;
            mType = src.mType;
            mChanges = src.mChanges;
            if (src.getPublicLink())
            {
                mPublicLink.reset(src.getPublicLink()->copy());
            }

            return *this;
        }

        // return public id of the set
        handle publicId() const
        {
            return mPublicLink ? mPublicLink->getPublicHandle() : UNDEF;
        }

        // return id of the user that owns this Set
        const handle& user() const { return mUser; }

        // return id of the Element that was set as cover, or UNDEF if none was set
        handle cover() const;

        // get creation timestamp
        const m_time_t& cts() const { return mCTs; }

        // get Set type
        SetType type() const { return mType; }

        // get public link info
        const PublicLinkSet* getPublicLink() const
        {
            return mPublicLink.get();
        }

        // set id of the user that owns this Set
        void setUser(handle uh) { mUser = uh; }

        // set id of the Element that will act as cover; pass UNDEF to remove cover
        void setCover(handle h);

        // set creation timestamp
        void setCTs(m_time_t ts) { mCTs = ts; }

        // set Set type
        void setType(SetType t) { mType = t; }

        // set public link info (take ownership)
        void setPublicLink(std::unique_ptr<PublicLinkSet>&& publicLink)
        {
            mPublicLink = std::move(publicLink);
        }

        // set public link info (No take ownership)
        void setPublicLink(const PublicLinkSet* publicLink)
        {
            mPublicLink.reset(publicLink ? publicLink->copy() : nullptr);
        }

        // replace internal parameters with the ones of 's', and mark any CH_XXX change
        bool updateWith(Set&& s);

        // apply attrs on top of the ones of 's' (useful for instances that only contain updates)
        void rebaseAttrsOn(const Set& s) { rebaseCommonAttrsOn(s.mAttrs.get()); }

        // mark a change to internal parameters (useful for app notifications)
        void setChanged(int changeType);

        // reset changes of internal parameters (call after app has been notified)
        void resetChanges() { mChanges = 0; }

        // return changes to internal parameters (useful for app notifications)
        unsigned long changes() const { return mChanges.to_ulong(); }

        // return true if internal parameter pointed out by changeType has changed (useful for app
        // notifications)
        bool hasChanged(uint64_t changeType) const
        {
            return validChangeType(changeType, CH_SIZE) ?
                       mChanges[static_cast<size_t>(changeType)] :
                       false;
        }

        // Returns true if Set is exported
        bool isExported() const
        {
            return publicId() != UNDEF;
        }

        bool serialize(std::string*) const override;
        static std::unique_ptr<Set> unserialize(std::string* d);

        enum // match MegaSet::CHANGE_TYPE_XXX values
        {
            CH_NEW,     // point out that this is a new Set
            CH_NAME,    // point out that 'name' attr has changed
            CH_COVER,   // point out that 'cover' attr has changed
            CH_REMOVED, // point out that this Set has been removed
            CH_EXPORTED,// point out that this Set has been exported (shared) or disabled (stopped being shared)

            CH_SIZE
        };

        enum : SetType
        {
            TYPE_ALBUM = 0,
            TYPE_PLAYLIST,

            TYPE_SIZE
        };

    private:
        handle mUser = UNDEF;
        m_time_t mCTs = 0; // creation timestamp
        SetType mType = TYPE_ALBUM;

        std::bitset<CH_SIZE> mChanges;

        std::unique_ptr<PublicLinkSet> mPublicLink;

        static const std::string coverTag; // "c", used for 'cover' attribute
    };

    using elementsmap_t = std::map<handle, SetElement>;
} //namespace

#endif

