#ifndef MEGA_USER_ATTRIBUTE_DEFINITION_H
#define MEGA_USER_ATTRIBUTE_DEFINITION_H

#include "user_attribute_types.h"

#include <string>
#include <unordered_map>

namespace mega
{

class UserAttrDefinition
{
public:
    static const UserAttrDefinition* get(attr_t at);

    static attr_t getTypeForName(const std::string& name);

    static inline size_t getDefaultMaxSize()
    {
        return MAX_USER_ATTRIBUTE_SIZE;
    }

    inline char scope() const
    {
        return mScope;
    }

    inline const std::string& name() const
    {
        return mName;
    }

    inline const std::string& longName() const
    {
        return mLongName;
    }

    inline bool versioningEnabled() const
    {
        return mUseVersioning;
    }

    inline size_t maxSize() const
    {
        return mMaxSize;
    }

private:
    UserAttrDefinition(std::string&& name,
                       std::string&& longName,
                       int customOptions = NONE); // [*^+#%$][!~](actual name)

    static const std::unordered_map<attr_t, const UserAttrDefinition>& getAllDefinitions();

    const std::string mName;
    const std::string mLongName;
    char mScope = ATTR_SCOPE_UNKNOWN;
    bool mUseVersioning = true;
    size_t mMaxSize = 0;

    enum Opt
    {
        NONE,
        DISABLE_VERSIONING,
        MAKE_PROTECTED,
        MAKE_PRIVATE = 4,
    };
};

} // namespace

#endif // MEGA_USER_ATTRIBUTE_DEFINITION_H
