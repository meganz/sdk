#pragma once

#include <string>
#include <vector>

/**
 * @brief A list of environment variable name pairs ( email and password) for accounts.
 */
class EnvVarAccounts
{
public:
    // Email and pass pair
    using ValueType = std::pair<std::string, std::string>;

    using NameType = std::pair<std::string, std::string>;

    /**
     * @brief EnvVarAccounts initialzes with a list of value
     * @param values The list of values
     */
    EnvVarAccounts(std::initializer_list<ValueType> values);

    /**
     * @brief EnvVarAccounts initializes with count copies of the value
     * @param count The count of copies
     * @param value The value of copies
     */
    EnvVarAccounts(size_t count, const ValueType& value);

    /**
     * @brief getVarValues gets a specified account's email and password
     * from environment variables
     *
     * @param i Specify which account. It needs to be in the range of size().
     * @return The pair of email and password values
     */
    ValueType getVarValues(size_t i) const;

    /**
     * @brief getVarNames gets a specified account's environment variable
     * names for its email and password
     *
     * @param i Specify which account. It needs to be in the range of size().
     * @return The pair of environment variables for the email and password
     */
    NameType getVarNames(size_t i) const;

    /**
     * @brief cloneVarNames returns a copy of the list of environment variable
     * name pairs ( email and password) of the accounts.
     *
     * @return a copy of the list
     */
    std::vector<NameType> cloneVarNames() const;

    size_t size() const;
private:
    std::vector<ValueType> mAccounts;
};

EnvVarAccounts& getEnvVarAccounts();

