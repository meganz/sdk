#ifndef MEGA_USER_ATTRIBUTE_MANAGER_H
#define MEGA_USER_ATTRIBUTE_MANAGER_H

#include "user_attribute.h"
#include "user_attribute_types.h"

#include <string>
#include <unordered_map>

namespace mega
{

class UserAttributeManager
{
public:
    void set(attr_t at, const std::string& value, const std::string& version);
    bool setIfNewVersion(attr_t at, const std::string& value, const std::string& version);
    bool setExpired(attr_t at);
    const UserAttribute* get(attr_t at) const;
    bool erase(attr_t at);
    bool eraseUpdateVersion(attr_t at, const std::string& version);

    void serializeAttributeFormatVersion(std::string& appendTo) const;
    static char unserializeAttributeFormatVersion(const char*& from);
    void serializeAttributes(std::string& appendTo) const;
    bool unserializeAttributes(const char*& from, const char* upTo, char formatVersion);

    static std::string getName(attr_t at);
    static std::string getLongName(attr_t at);
    static attr_t getType(const std::string& name);
    static char getScope(attr_t at);
    static int getVersioningEnabled(attr_t at);
    static size_t getMaxSize(attr_t at);

    void cacheNonExistingAttributes()
    {
        // used to avoid fetch from servers for attributes known to not exist
        mCacheNonExistingAttributes = true;
    }

private:
    bool setNotExisting(attr_t at);
    bool isValid(attr_t at) const; // not Expired and not cached as Not Existing

    std::unordered_map<attr_t, UserAttribute> mAttributes;
    bool mCacheNonExistingAttributes = false;
};

} // namespace

#endif // MEGA_USER_ATTRIBUTE_MANAGER_H
