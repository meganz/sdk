#include "mega/user_attribute_definition.h"

#include <algorithm>
#include <array>
#include <cassert>

using namespace std;

namespace mega
{

const UserAttrDefinition* UserAttrDefinition::get(attr_t at)
{
    const unordered_map<attr_t, const UserAttrDefinition>& defs = getAllDefinitions();
    const auto it = defs.find(at);
    return it == defs.end() ? nullptr : &it->second;
}

attr_t UserAttrDefinition::getTypeForName(const string& name)
{
    for (const auto& d: getAllDefinitions())
    {
        if (d.second.name() == name)
            return d.first;
    }

    return ATTR_UNKNOWN; // attribute not recognized
}

UserAttrDefinition::UserAttrDefinition(string&& name, string&& longName, int customOptions):
    mName(std::move(name)),
    mLongName(std::move(longName))
{
    if (mName.empty())
    {
        assert(!mName.empty());
        return;
    }

    switch (mName[0])
    {
        case ATTR_SCOPE_PUBLIC:
        case ATTR_SCOPE_PROTECTED:
        case ATTR_SCOPE_PRIVATE:
        case ATTR_SCOPE_PRIVATE_ENCRYPTED:
        case ATTR_SCOPE_BUSINESS:
        case ATTR_SCOPE_BUSINESS_ENCRYPTED:
            mScope = mName[0];
    }

    bool hasModifier =
        mScope != ATTR_SCOPE_UNKNOWN && mName.size() > 1 && (mName[1] == '!' || mName[1] == '~');

    mMaxSize = hasModifier ? MAX_USER_VAR_SIZE : MAX_USER_ATTRIBUTE_SIZE;

    mUseVersioning = !(customOptions & DISABLE_VERSIONING);

    // allow setting (only one) explicit scope when prefix did not contain it
    if (customOptions & MAKE_PROTECTED)
    {
        assert(mScope == ATTR_SCOPE_UNKNOWN && !(customOptions & MAKE_PRIVATE));
        mScope = ATTR_SCOPE_PROTECTED;
    }

    else if (customOptions & MAKE_PRIVATE)
    {
        assert(mScope == ATTR_SCOPE_UNKNOWN && !(customOptions & MAKE_PROTECTED));
        mScope = ATTR_SCOPE_PRIVATE;
    }
}

const unordered_map<attr_t, const UserAttrDefinition>& UserAttrDefinition::getAllDefinitions()
{
    // Creating this map all at once should be fine in terms of complexity - populated once, and
    // most likely in a secondary thread. It also allows clean code and avoids having to write
    // attribute names multiple times.
    static unordered_map<attr_t, const UserAttrDefinition> defs{
        {ATTR_AVATAR,                    {"+a", "AVATAR", DISABLE_VERSIONING}                           },
        {ATTR_FIRSTNAME,                 {"firstname", "FIRSTNAME", DISABLE_VERSIONING | MAKE_PROTECTED}},
        {ATTR_LASTNAME,                  {"lastname", "LASTNAME", DISABLE_VERSIONING | MAKE_PROTECTED}  },
        {ATTR_AUTHRING,                  {"*!authring", "AUTHRING"}                                     },
        {ATTR_LAST_INT,                  {"*!lstint", "LAST_INT"}                                       },
        {ATTR_ED25519_PUBK,              {"+puEd255", "ED25519_PUBK"}                                   },
        {ATTR_CU25519_PUBK,              {"+puCu255", "CU25519_PUBK"}                                   },
        {ATTR_KEYRING,                   {"*keyring", "KEYRING"}                                        },
        {ATTR_SIG_RSA_PUBK,              {"+sigPubk", "SIG_RSA_PUBK"}                                   },
        {ATTR_SIG_CU255_PUBK,            {"+sigCu255", "SIG_CU255_PUBK"}                                },
        {ATTR_COUNTRY,                   {"country", "COUNTRY", DISABLE_VERSIONING | MAKE_PRIVATE}      },
        {ATTR_BIRTHDAY,                  {"birthday", "BIRTHDAY", DISABLE_VERSIONING | MAKE_PRIVATE}    },
        {ATTR_BIRTHMONTH,                {"birthmonth", "BIRTHMONTH", DISABLE_VERSIONING | MAKE_PRIVATE}},
        {ATTR_BIRTHYEAR,                 {"birthyear", "BIRTHYEAR", DISABLE_VERSIONING | MAKE_PRIVATE}  },
        {ATTR_LANGUAGE,                  {"^!lang", "LANGUAGE", DISABLE_VERSIONING}                     },
        {ATTR_PWD_REMINDER,              {"^!prd", "PWD_REMINDER", DISABLE_VERSIONING}                  },
        {ATTR_DISABLE_VERSIONS,          {"^!dv", "DISABLE_VERSIONS", DISABLE_VERSIONING}               },
        {ATTR_CONTACT_LINK_VERIFICATION, {"^!clv", "CONTACT_LINK_VERIFICATION"}                         },
        {ATTR_RICH_PREVIEWS,             {"*!rp", "RICH_PREVIEWS", DISABLE_VERSIONING}                  },
        {ATTR_RUBBISH_TIME,              {"^!rubbishtime", "RUBBISH_TIME", DISABLE_VERSIONING}          },
        {ATTR_LAST_PSA,                  {"^!lastPsa", "LAST_PSA", DISABLE_VERSIONING}                  },
        {ATTR_STORAGE_STATE,             {"^!usl", "STORAGE_STATE", DISABLE_VERSIONING}                 },
        {ATTR_GEOLOCATION,               {"*!geo", "GEOLOCATION", DISABLE_VERSIONING}                   },
        {ATTR_CAMERA_UPLOADS_FOLDER,     {"*!cam", "CAMERA_UPLOADS_FOLDER"}                             },
        {ATTR_MY_CHAT_FILES_FOLDER,      {"*!cf", "MY_CHAT_FILES_FOLDER", DISABLE_VERSIONING}           },
        {ATTR_PUSH_SETTINGS,             {"^!ps", "PUSH_SETTINGS", DISABLE_VERSIONING}                  },
        {ATTR_UNSHAREABLE_KEY,           {"*~usk", "UNSHAREABLE_KEY"}                                   },
        {ATTR_ALIAS,                     {"*!>alias", "ALIAS"}                                          },
        {ATTR_AUTHCU255,                 {"*!authCu255", "AUTHCU255"}                                   },
        {ATTR_DEVICE_NAMES,              {"*!dn", "DEVICE_NAMES"}                                       },
        {ATTR_MY_BACKUPS_FOLDER,         {"^!bak", "MY_BACKUPS_FOLDER"}                                 },
        {ATTR_COOKIE_SETTINGS,           {"^!csp", "COOKIE_SETTINGS", DISABLE_VERSIONING}               },
        {ATTR_JSON_SYNC_CONFIG_DATA,     {"*~jscd", "JSON_SYNC_CONFIG_DATA"}                            },
        {ATTR_NO_CALLKIT,                {"^!nokit", "NO_CALLKIT", DISABLE_VERSIONING}                  },
        {ATTR_KEYS,                      {"^!keys", "KEYS"}                                             },
        {ATTR_APPS_PREFS,                {"*!aPrefs", "APPS_PREFS"}                                     },
        {ATTR_CC_PREFS,                  {"*!ccPref", "CC_PREFS"}                                       },
        {ATTR_VISIBLE_WELCOME_DIALOG,    {"^!weldlg", "VISIBLE_WELCOME_DIALOG"}                         },
        {ATTR_VISIBLE_TERMS_OF_SERVICE,  {"^!tos", "VISIBLE_TERMS_OF_SERVICE"}                          },
        {ATTR_PWM_BASE,                  {"pwmh", "PWM_BASE", DISABLE_VERSIONING | MAKE_PRIVATE}        },
        {ATTR_ENABLE_TEST_NOTIFICATIONS, {"^!tnotif", "ENABLE_TEST_NOTIFICATIONS"}                      },
        {ATTR_LAST_READ_NOTIFICATION,    {"^!lnotif", "LAST_READ_NOTIFICATION"}                         },
        {ATTR_LAST_ACTIONED_BANNER,      {"^!lbannr", "LAST_ACTIONED_BANNER"}                           },
        {ATTR_ENABLE_TEST_SURVEYS,       {"^!tsur", "ENABLE_TEST_SURVEYS", DISABLE_VERSIONING}          },
    };

    return defs;
}

} // namespace
