#include "mega/user_attribute_manager.h"

#include "mega/attrmap.h" // for AttrMap
#include "mega/user_attribute_definition.h"
#include "mega/utils.h" // for MemAccess

#include <cassert>
#include <limits>

using namespace std;

namespace mega
{

const string* UserAttrManager::getAttrRawValue(attr_t at) const
{
    if (at == ATTR_AVATAR)
        return nullptr; // its value is never cached

    auto itAttr = mAttrs.find(at);
    if (itAttr == mAttrs.end() || itAttr->second.isCachedNotExisting())
        return nullptr;

    return &itAttr->second.value();
}

const string* UserAttrManager::getAttrVersion(attr_t at) const
{
    auto itAttr = mAttrs.find(at);
    if (itAttr == mAttrs.end() || itAttr->second.isCachedNotExisting())
        return nullptr;

    return &itAttr->second.version();
}

void UserAttrManager::setAttr(attr_t at, const string& value, const string& version)
{
    const UserAttrDefinition* d = UserAttrDefinition::get(at);
    if (!d)
    {
        assert(d);
        return;
    }

    UserAttr& attr = mAttrs.try_emplace(at, *d).first->second;
    if (at == ATTR_AVATAR) // avatar is saved to disc, keep only its version
    {
        attr.set("", version);
    }
    else
    {
        attr.set(value, version);
    }
}

bool UserAttrManager::setAttrIfNewVersion(attr_t at, const string& value, const string& version)
{
    const UserAttrDefinition* d = UserAttrDefinition::get(at);
    if (!d)
    {
        assert(d);
        return false;
    }

    auto insertResult = mAttrs.try_emplace(at, *d);
    UserAttr& attr = insertResult.first->second;
    if (!insertResult.second && attr.version() == version)
    {
        return false;
    }

    if (at == ATTR_AVATAR) // avatar is saved to disc, keep only its version
    {
        attr.set("", version);
    }
    else
    {
        attr.set(value, version);
    }
    return true;
}

bool UserAttrManager::setAttrNotExisting(attr_t at)
{
    const UserAttrDefinition* d = UserAttrDefinition::get(at);
    if (!d)
    {
        assert(d); // undefined attribute
        return false;
    }

    UserAttr& attr = mAttrs.try_emplace(at, *d).first->second;
    if (attr.isCachedNotExisting())
        return false;

    attr.setNotExisting();
    return true;
}

bool UserAttrManager::isAttrNotExisting(attr_t at) const
{
    auto itAttr = mAttrs.find(at);
    return itAttr != mAttrs.end() && itAttr->second.isCachedNotExisting();
}

void UserAttrManager::setAttrExpired(attr_t at)
{
    const auto it = mAttrs.find(at);
    if (it != mAttrs.end())
        it->second.setExpired();
}

bool UserAttrManager::isAttrValid(attr_t at) const
{
    auto itAttr = mAttrs.find(at);
    return itAttr != mAttrs.end() && itAttr->second.isValid();
}

bool UserAttrManager::eraseAttr(attr_t at)
{
    return mAttrs.erase(at) > 0;
}

void UserAttrManager::serializeAttributeFormatVersion(string& appendTo) const
{
    static constexpr char attributeFormatVersion = '2';
    // Version 1: attributes are serialized along with its version
    // Version 2: size of attributes use 4B (uint32_t) instead of 2B (unsigned short)
    appendTo += attributeFormatVersion;
}

char UserAttrManager::unserializeAttributeFormatVersion(const char*& from)
{
    assert(from != nullptr);
    char attrVersion = *from;
    ++from;
    return attrVersion;
}

void UserAttrManager::serializeAttributes(string& d) const
{
    assert(mAttrs.size() <= numeric_limits<unsigned char>::max());

    // serialize attr count
    auto attrCount = std::count_if(mAttrs.begin(),
                                   mAttrs.end(),
                                   [](const auto& attr)
                                   {
                                       return attr.second.isValid();
                                   });
    assert(attrCount <= numeric_limits<unsigned char>::max());
    d += static_cast<unsigned char>(attrCount);

    for (const auto& a: mAttrs)
    {
        if (!a.second.isValid())
            continue;

        // serialize type
        d.append(reinterpret_cast<const char*>(&a.first), sizeof(a.first));

        // serialize value
        const string& value = a.second.value();
        assert(value.size() <= numeric_limits<uint32_t>::max());
        uint32_t valueSize = static_cast<uint32_t>(value.size());
        d.append(reinterpret_cast<char*>(&valueSize), sizeof(valueSize));
        d.append(value.data(), valueSize);

        // serialize version
        const string& version = a.second.version();
        assert(version.size() <= numeric_limits<uint16_t>::max());
        uint16_t versionSize = static_cast<uint16_t>(version.size());
        d.append(reinterpret_cast<char*>(&versionSize), sizeof(versionSize));
        if (versionSize)
            d.append(version.data(), versionSize);
    }
}

bool UserAttrManager::unserializeAttributes(const char*& from, const char* upTo, char formatVersion)
{
    if (formatVersion == '1' || formatVersion == '2')
    {
        if (from + sizeof(char) > upTo)
            return false;
        unsigned char attrCount = *from++;
        size_t sizeLength = (formatVersion == '1') ? 2 : 4;
        // formatVersion == 1 -> size of value uses 2 bytes
        // formatVersion == 2 -> size of value uses 4 bytes

        for (int i = 0; i < attrCount; i++)
        {
            if (from + sizeof(attr_t) + sizeLength > upTo)
                return false;

            // type
            attr_t at = MemAccess::get<attr_t>(from);
            from += sizeof(at);

            // size of value
            uint32_t valueSize = formatVersion == '1' ? MemAccess::get<uint16_t>(from) :
                                                        MemAccess::get<uint32_t>(from);
            from += sizeLength;

            if (from + valueSize + sizeof(uint16_t) > upTo)
                return false;

            // skip value for now
            auto* tmpVal = from;
            from += valueSize;

            // size of version
            uint16_t versionSize = MemAccess::get<uint16_t>(from);
            from += sizeof(versionSize);

            auto* tmpVer = from;
            if (versionSize)
            {
                if (from + versionSize > upTo)
                    return false;

                from += versionSize;
            }

            // keep the ones that were not already loaded (i.e. by `ug` for own user), or
            // have been removed
            if (!isAttrValid(at))
            {
                string value{tmpVal, valueSize};
                string version{(versionSize ? string{tmpVer, versionSize} : string{})};
                setAttr(at, value, version);
            }
        }
    }
    else if (formatVersion == '\0')
    {
        // ignore user attributes in this format
        AttrMap attrmap;
        if ((from >= upTo) || !(from = attrmap.unserialize(from, upTo)))
            return false;
    }

    return true;
}

string UserAttrManager::getAttrName(attr_t at)
{
    const UserAttrDefinition* ad = UserAttrDefinition::get(at);
    return ad ? ad->name() : string{};
}

string UserAttrManager::getAttrLongName(attr_t at)
{
    const UserAttrDefinition* ad = UserAttrDefinition::get(at);
    return ad ? ad->longName() : string{};
}

attr_t UserAttrManager::getAttrType(const string& name)
{
    return UserAttrDefinition::getTypeForName(name);
}

char UserAttrManager::getAttrScope(attr_t at)
{
    const UserAttrDefinition* ad = UserAttrDefinition::get(at);
    return ad ? ad->scope() : static_cast<char>(ATTR_SCOPE_UNKNOWN);
}

int UserAttrManager::getAttrVersioningEnabled(attr_t at)
{
    static constexpr int VERSIONING_ENABLED_UNKNOWN = -1;
    if (at == ATTR_STORAGE_STATE)
        return VERSIONING_ENABLED_UNKNOWN; // help block putua for this attribute

    const UserAttrDefinition* ad = UserAttrDefinition::get(at);
    return ad ? ad->versioningEnabled() : VERSIONING_ENABLED_UNKNOWN;
}

size_t UserAttrManager::getAttrMaxSize(attr_t at)
{
    const UserAttrDefinition* ad = UserAttrDefinition::get(at);
    return ad ? ad->maxSize() : UserAttrDefinition::getDefaultMaxSize();
}

} // namespace
