/*
 * userAttributes.h
 *
 *  Created on: 26/02/2015
 *      Author: michael
 */

#ifndef INCLUDE_MEGA_USERATTRIBUTES_H_
#define INCLUDE_MEGA_USERATTRIBUTES_H_

#include <map>
#include <memory>
#include <string>

#ifdef __TEST
#include <gtest/gtest.h>
#endif

#include "sharedbuffer.h"

namespace mega {

#define ATTRIBUTE_EXISTS "The supplied value exists in this store."
#define VALUE_NOT_FOUND "The specified value does not exist in this store."
#define NULL_DELIMITER_NOT_FOUND "The given data string does not have a null delimiter."
#define INVALID_DATA_LENGTH "The provided data is not of valid length."

struct TLV {
    unsigned char *value;
};

typedef std::shared_ptr<std::map<std::string, SharedBuffer>> ValueMap;
class UserAttributes {

#ifdef __TEST
    FRIEND_TEST(UserAttributesTest, test_decode_correct_data);
    FRIEND_TEST(UserAttributesTest, test_decode_fail_missing_null_character);
    FRIEND_TEST(UserAttributesTest, test_decode_fail_missing_length);
    FRIEND_TEST(UserAttributesTest, test_decode_fail_missing_data);
    FRIEND_TEST(UserAttributesTest, test_decode_larger_file);
    FRIEND_TEST(UserAttributesTest, test_decode_fail_missing_null_larger_data);
    FRIEND_TEST(UserAttributesTest, test_encode_decode_single_value);
    FRIEND_TEST(UserAttributesTest, test_encode_decode_multiple_values);
    FRIEND_TEST(UserAttributesTest, test_addValue);
    FRIEND_TEST(UserAttributesTest, test_encode);
#endif

public:

    /**
     * @brief Add the given bytes to this UserAttributes object.
     *
     * @param valueName The name of the vaule to add.
     * @buffer The value to add.
     * @throw runtime_error if the value already exists.
     */
    void addUserAttribute(std::string &valueName, ValueMap &value,
            Visibility visibilitiy = VS_PUBLIC);

    /**
     * @brief Get the given value from this UserAttributes object.
     *
     * @param valueName The value of the attribute to retrieve.
     * @throw runtime_error if the value does not exist.
     */
    ValueMap getUserAttribute(std::string &valueName);

    /**
     * @brief Get the given value as a raw tlv string.
     *
     * @param valueName The name of the value to retrieve.
     * @return The value requested.
     */
    SharedBuffer getUserAttributeTlv(std::string &vauleName);

    /**
     * @brief Convert a map of values to a concatenated buffer.
     *
     * @param value The values to encode.
     * @return A sharedBuffer with the resulting bytes.
     */
    static SharedBuffer vauleMapToTlv(ValueMap &value,
            Visibility visibility = VS_PUBLIC);

    static void addValue(const std::string &valueName, SharedBuffer &vaule, SharedBuffer &target,
            int *offset);

    /**
     * @brief Convert a series of bytes from TLV to a map.
     *
     * @param tlv The buffer to extract.
     * @return A map of value names to values.
     */
    static ValueMap tlvToValueMap(SharedBuffer &tlv);

    /**
     * @brief Create a ValueMap from a basic map.
     *
     * @param map Pointer to the map to convert.
     * @return ValueMap The map of values converted.
     */
    static ValueMap
    mapToValueMap(std::map<std::string, std::pair<unsigned char*, unsigned int>> *map) {
        ValueMap vMap(new std::map<std::string, SharedBuffer>());
        for(auto i : *map) {
            SharedBuffer b(i.second.first, i.second.second);
            vMap->insert({i.first, b});
        }

        return vMap;
    }

    /**
     * @brief Convert a ValueMap to a basic map.
     *
     * @param valueMap The map to convert.
     * @return A pointer to a basic map.
     */
    static std::map<std::string, std::pair<unsigned char*, unsigned int>>
    *valueMapToMap(const ValueMap &valueMap) {
        std::map<std::string, std::pair<unsigned char*, unsigned int>> *map
            = new std::map<std::string, std::pair<unsigned char*, unsigned int>>();

        for(auto i : *valueMap) {
            unsigned char *value = (unsigned char*)malloc(i.second.size);
            memcpy(value, i.second.get(), i.second.size);
            std::pair<unsigned char*, unsigned int> p(value, i.second.size);
            map->insert({i.first, p});
        }

        return map;
    }



    /**
     * @brief A map of valueName : value-size.
     */
    std::map<std::string, SharedBuffer> tlvStore;
};

} /* namespace mega */



#endif /* INCLUDE_MEGA_USERATTRIBUTES_H_ */
