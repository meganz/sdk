#ifndef MEGA_USER_ATTRIBUTE_H
#define MEGA_USER_ATTRIBUTE_H

#include <string>

namespace mega
{

class UserAttrDefinition;

class UserAttr
{
public:
    UserAttr(const UserAttrDefinition& def):
        mDefinition{def}
    {}

    void set(const std::string& value, const std::string& version);

    bool useVersioning() const;

    inline bool isValid() const
    {
        return mState == State::VALID;
    }

    inline void setExpired()
    {
        mState = State::EXPIRED;
    }

    inline bool isExpired() const
    {
        return mState == State::EXPIRED;
    }

    inline void setNotExisting()
    {
        mState = State::CACHED_NOT_EXISTING;
        mValue.clear();
    }

    inline bool isCachedNotExisting() const
    {
        return mState == State::CACHED_NOT_EXISTING;
    }

    inline const std::string& value() const
    {
        return mValue;
    }

    inline const std::string& version() const
    {
        return mVersion;
    }

private:
    const UserAttrDefinition& mDefinition;
    std::string mValue;
    std::string mVersion;

    enum class State
    {
        VALID,
        EXPIRED,
        CACHED_NOT_EXISTING,
    };
    State mState = State::VALID;
};

} // namespace

#endif // MEGA_USER_ATTRIBUTE_H
