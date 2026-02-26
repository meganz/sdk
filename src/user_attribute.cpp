#include "mega/user_attribute.h"

#include "mega/user_attribute_definition.h"

#include <unordered_map>

using namespace std;

namespace mega
{

void UserAttribute::set(const string& value, const string& version)
{
    mValue = value;

    // Version is stored even for attributes marked as not supporting it.
    // Notably "firstname" does come with version populated, but it is not used in case of update.
    mVersion = version;

    mState = State::VALID;
}

bool UserAttribute::useVersioning() const
{
    return mDefinition.versioningEnabled();
}

} // namespace
