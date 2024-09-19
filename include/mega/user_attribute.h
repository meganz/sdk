#ifndef MEGA_USER_ATTRIBUTE_H
#define MEGA_USER_ATTRIBUTE_H

#include <string>

namespace mega
{

class UserAttributeDefinition;

class UserAttribute
{
public:
    UserAttribute(const UserAttributeDefinition& def):
        mDefinition{def}
    {}

    void set(const std::string& value, const std::string& version);

    bool useVersioning() const;

    bool isValid() const
    {
        return mState == State::VALID;
    }

    void setExpired()
    {
        mState = State::EXPIRED;
    }

    bool isExpired() const
    {
        return mState == State::EXPIRED;
    }

    void setNotExisting()
    {
        mState = State::CACHED_NOT_EXISTING;
        mValue.clear();
    }

    bool isNotExisting() const
    {
        return mState == State::CACHED_NOT_EXISTING;
    }

    const std::string& value() const
    {
        return mValue;
    }

    const std::string& version() const
    {
        return mVersion;
    }

private:
    const UserAttributeDefinition& mDefinition;
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
