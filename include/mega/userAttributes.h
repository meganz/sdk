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
#include <algorithm>

#ifdef __TEST
#include <gtest/gtest.h>
#endif
#include "megaapi.h"
#include "sharedbuffer.h"

namespace mega {

#define ATTRIBUTE_EXISTS "The supplied value exists in this store."
#define VALUE_NOT_FOUND "The specified value does not exist in this store."
#define NULL_DELIMITER_NOT_FOUND "The given data string does not have a null delimiter."
#define INVALID_DATA_LENGTH "The provided data is not of valid length."

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
     * @param valueName The name of the value to add.
     * @param value The value to add.
     * @param visibility The visibility of the value.
     * @throw runtime_error if the value already exists.
     */
    void addUserAttribute(std::string &valueName, ValueMap &value,
            Visibility visibilitiy = M_VS_PUBLIC);

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
    SharedBuffer getUserAttributeTlv(std::string &valueName);

    /**
     * @brief Convert a map of values to a concatenated buffer.
     *
     * @param value The values to encode.
     * @param visibility The visibility of the attribute - public or private.
     * @return A sharedBuffer with the resulting bytes.
     */
    static SharedBuffer valueMapToTlv(ValueMap &value,
            Visibility visibility = M_VS_PUBLIC);

    /**
     * @brief Add a value to the tlv string of user attributes.
     *
     * @param valueName The name of the value to add.
     * @param value The value to add.
     * @param target The target tlv string to add it to.
     * @param offset The offset from the beginning of the string to add the value to.
     */
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
     * @deprecated Not currently used in the MEGA SDK.
     *
     * @param map Pointer to the map to convert.
     * @return ValueMap The map of values converted.
     */
    static ValueMap
    mapToValueMap(std::map<std::string, std::pair<unsigned char*, unsigned int>> *map) 
	{
        ValueMap vMap(new std::map<std::string, SharedBuffer>());
		std::for_each(map->begin(), map->end(), [&vMap](std::map<std::string, std::pair<unsigned char*, unsigned int>>::value_type &i)
		{
            SharedBuffer buffer(i.second.first, i.second.second);
            vMap->insert(std::make_pair(i.first, buffer));
		});

        return vMap;
    }

    /**
     * @brief Convert a ValueMap to a basic map.
     *
     * @param valueMap The map to convert.
     * @return A pointer to a basic map.
     */
    static std::map<std::string, std::pair<unsigned char*, unsigned int>>
    *valueMapToMap(const ValueMap &valueMap)
    {
        std::map<std::string, std::pair<unsigned char*, unsigned int>> *map
            = new std::map<std::string, std::pair<unsigned char*, unsigned int>>();
		std::for_each(valueMap->begin(), valueMap->end(), [&map](std::map<std::string, SharedBuffer>::value_type &i)
		{
            unsigned char *value = (unsigned char*)malloc(i.second.size);
            memcpy(value, i.second.get(), i.second.size);
            std::pair<unsigned char*, unsigned int> p(value, i.second.size);
            map->insert(std::make_pair(i.first, p));
		});

        return map;
    }

    /**
     * @brief Convert an array of tlv values to a value map.
     *
     * @param tlvArray The array of tlv values.
     * @param length The length of the array.
     * @return A ValueMap containing the values in the array.
     */
    static ValueMap tlvArrayToValueMap(TLV *tlvArray, unsigned int length)
    {
        ValueMap vMap(new std::map<std::string, SharedBuffer>);
        for(int x = 0; x < length; x++)
        {
            SharedBuffer buffer(tlvArray[x].getValue(), tlvArray[x].getLength());
            vMap->insert(std::make_pair(std::string(tlvArray[x].getType()), buffer));
        }
        return vMap;
    }

    /**
     * @brief Convert a ValueMap to a tlv array.
     *
     * @param map The ValueMap to convert.
     * @return A tlv array with the values contained in the ValueMap.
     */
    static TLV *valueMapToTLVarray(const ValueMap &map)
    {
        TLV *tlvArray = (TLV*)malloc(map->size() * sizeof(TLV));
        memset(tlvArray, 0, map->size() * sizeof(TLV));
        int x = 0;
		std::for_each(map->begin(), map->end(), [&](std::map<std::string, SharedBuffer>::value_type &i) 
        {
		    TLV tlv(i.first.c_str(), i.second.size, i.second.get());
		    tlvArray[x] = tlv;
            x++;
		});

        return tlvArray;
    }

    /**
     * @brief A map of valueName : value-size.
     */
    std::map<std::string, SharedBuffer> tlvStore;
};

} /* namespace mega */



#endif /* INCLUDE_MEGA_USERATTRIBUTES_H_ */
