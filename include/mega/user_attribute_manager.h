#ifndef MEGA_USER_ATTRIBUTE_MANAGER_H
#define MEGA_USER_ATTRIBUTE_MANAGER_H

#include "user_attribute.h"
#include "user_attribute_types.h"

#include <string>
#include <unordered_map>

namespace mega
{

class UserAttrManager
{
public:
    const std::string* getAttrRawValue(attr_t at) const;
    const std::string* getAttrVersion(attr_t at) const;
    void setAttr(attr_t at, const std::string& value, const std::string& version);
    bool setAttrIfNewVersion(attr_t at, const std::string& value, const std::string& version);
    bool setAttrNotExisting(attr_t at);
    bool isAttrNotExisting(attr_t at) const;
    void setAttrExpired(attr_t at);
    bool isAttrValid(attr_t at) const; // not Expired and not Cached as Not Existing
    bool eraseAttr(attr_t at);

    void serializeAttributeFormatVersion(std::string& appendTo) const;
    static char unserializeAttributeFormatVersion(const char*& from);
    void serializeAttributes(std::string& appendTo) const;
    bool unserializeAttributes(const char*& from, const char* upTo, char formatVersion);

    static std::string getAttrName(attr_t at);
    static std::string getAttrLongName(attr_t at);
    static attr_t getAttrType(const std::string& name);
    static char getAttrScope(attr_t at);
    static int getAttrVersioningEnabled(attr_t at);
    static size_t getAttrMaxSize(attr_t at);

private:
    std::unordered_map<attr_t, UserAttr> mAttrs;
};

} // namespace

#endif // MEGA_USER_ATTRIBUTE_MANAGER_H
