#include "mega/user_attribute_manager.h"

#include "mega/attrmap.h" // for AttrMap
#include "mega/user_attribute_definition.h"
#include "mega/utils.h" // for MemAccess

#include <cassert>
#include <limits>

using namespace std;

namespace mega
{

void UserAttributeManager::set(attr_t at, const string& value, const string& version)
{
    const UserAttributeDefinition* d = UserAttributeDefinition::get(at);
    if (!d)
    {
        assert(d);
        return;
    }

    UserAttribute& attr = mAttributes.try_emplace(at, *d).first->second;
    if (at == ATTR_AVATAR) // avatar is saved to disc, keep only its version
    {
        attr.set("", version);
    }
    else
    {
        attr.set(value, version);
    }
}

bool UserAttributeManager::setIfNewVersion(attr_t at, const string& value, const string& version)
{
    const UserAttributeDefinition* d = UserAttributeDefinition::get(at);
    if (!d)
    {
        assert(d);
        return false;
    }

    auto insertResult = mAttributes.try_emplace(at, *d);
    UserAttribute& attr = insertResult.first->second;
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

bool UserAttributeManager::setNotExisting(attr_t at)
{
    const UserAttributeDefinition* d = UserAttributeDefinition::get(at);
    if (!d)
    {
        assert(d); // undefined attribute
        return false;
    }

    UserAttribute& attr = mAttributes.try_emplace(at, *d).first->second;
    if (attr.isNotExisting())
        return false;

    attr.setNotExisting();
    return true;
}

void UserAttributeManager::setExpired(attr_t at)
{
    const auto it = mAttributes.find(at);
    if (it != mAttributes.end())
        it->second.setExpired();
}

bool UserAttributeManager::isValid(attr_t at) const
{
    auto itAttr = mAttributes.find(at);
    return itAttr != mAttributes.end() && itAttr->second.isValid();
}

const UserAttribute* UserAttributeManager::get(attr_t at) const
{
    auto itAttr = mAttributes.find(at);
    return itAttr == mAttributes.end() ? nullptr : &itAttr->second;
}

bool UserAttributeManager::erase(attr_t at)
{
    if (mCacheNonExistingAttributes)
    {
        return setNotExisting(at);
    }
    else
    {
        return mAttributes.erase(at) > 0;
    }
}

bool UserAttributeManager::eraseUpdateVersion(attr_t at, const std::string& version)
{
    auto itAttr{mAttributes.find(at)};
    if (itAttr != mAttributes.end() &&
        (itAttr->second.isValid() || itAttr->second.version() != version))
    {
        bool notExisting = itAttr->second.isNotExisting();
        itAttr->second.set("", version);
        notExisting ? itAttr->second.setNotExisting() : itAttr->second.setExpired();
        return true;
    }
    return false;
}

void UserAttributeManager::serializeAttributeFormatVersion(string& appendTo) const
{
    static constexpr char attributeFormatVersion = '2';
    // Version 1: attributes are serialized along with its version
    // Version 2: size of attributes use 4B (uint32_t) instead of 2B (unsigned short)
    appendTo += attributeFormatVersion;
}

char UserAttributeManager::unserializeAttributeFormatVersion(const char*& from)
{
    assert(from != nullptr);
    char attrVersion = *from;
    ++from;
    return attrVersion;
}

void UserAttributeManager::serializeAttributes(string& d) const
{
    assert(mAttributes.size() <= numeric_limits<unsigned char>::max());

    // serialize attr count
    auto attrCount = std::count_if(mAttributes.begin(),
                                   mAttributes.end(),
                                   [](const auto& attr)
                                   {
                                       return attr.second.isValid();
                                   });
    assert(attrCount <= numeric_limits<unsigned char>::max());
    d += static_cast<unsigned char>(attrCount);

    for (const auto& a: mAttributes)
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

bool UserAttributeManager::unserializeAttributes(const char*& from,
                                                 const char* upTo,
                                                 char formatVersion)
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
            if (!isValid(at))
            {
                string value{tmpVal, valueSize};
                string version{(versionSize ? string{tmpVer, versionSize} : string{})};
                set(at, value, version);
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

string UserAttributeManager::getName(attr_t at)
{
    const UserAttributeDefinition* ad = UserAttributeDefinition::get(at);
    return ad ? ad->name() : string{};
}

string UserAttributeManager::getLongName(attr_t at)
{
    const UserAttributeDefinition* ad = UserAttributeDefinition::get(at);
    return ad ? ad->longName() : string{};
}

attr_t UserAttributeManager::getType(const string& name)
{
    return UserAttributeDefinition::getTypeForName(name);
}

char UserAttributeManager::getScope(attr_t at)
{
    const UserAttributeDefinition* ad = UserAttributeDefinition::get(at);
    return ad ? ad->scope() : static_cast<char>(ATTR_SCOPE_UNKNOWN);
}

int UserAttributeManager::getVersioningEnabled(attr_t at)
{
    static constexpr int VERSIONING_ENABLED_UNKNOWN = -1;
    if (at == ATTR_STORAGE_STATE)
        return VERSIONING_ENABLED_UNKNOWN; // help block putua for this attribute

    const UserAttributeDefinition* ad = UserAttributeDefinition::get(at);
    return ad ? ad->versioningEnabled() : VERSIONING_ENABLED_UNKNOWN;
}

size_t UserAttributeManager::getMaxSize(attr_t at)
{
    const UserAttributeDefinition* ad = UserAttributeDefinition::get(at);
    return ad ? ad->maxSize() : UserAttributeDefinition::getDefaultMaxSize();
}

} // namespace
