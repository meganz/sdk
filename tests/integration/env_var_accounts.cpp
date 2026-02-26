#include "env_var_accounts.h"

#include "mega/utils.h"

using mega::Utils;

EnvVarAccounts::EnvVarAccounts(std::initializer_list<ValueType> values)
    :mAccounts{values}
{
}

EnvVarAccounts::EnvVarAccounts(size_t count, const ValueType& value)
    :mAccounts(count, value)
{
}

EnvVarAccounts& getEnvVarAccounts()
{
    static EnvVarAccounts accounts {
        {"MEGA_EMAIL", "MEGA_PWD"},
        {"MEGA_EMAIL_AUX", "MEGA_PWD_AUX"},
        {"MEGA_EMAIL_AUX2", "MEGA_PWD_AUX2"},
    };
    return accounts;
}

EnvVarAccounts::ValueType EnvVarAccounts::getVarValues(size_t i) const
{
    assert(i < mAccounts.size());
    const auto& [email, pass] = mAccounts.at(i);
    return {Utils::getenv(email, ""), Utils::getenv(pass, "")};
}

EnvVarAccounts::NameType EnvVarAccounts::getVarNames(size_t i) const
{
    assert(i < mAccounts.size());
    return mAccounts.at(i);
}

std::vector<EnvVarAccounts::NameType> EnvVarAccounts::cloneVarNames() const
{
    return mAccounts;
}

size_t EnvVarAccounts::size() const
{
    return mAccounts.size();
}

