/**
 * @file mega/attrmap.h
 * @brief Class for manipulating file attributes
 *
 * (c) 2013-2014 by Mega Limited, Auckland, New Zealand
 *
 * This file is part of the MEGA SDK - Client Access Engine.
 *
 * Applications using the MEGA API must present a valid application key
 * and comply with the the rules set forth in the Terms of Service.
 *
 * The MEGA SDK is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * @copyright Simplified (2-clause) BSD License.
 *
 * You should have received a copy of the license along with this
 * program.
 */

#ifndef MEGA_ATTRMAP_H
#define MEGA_ATTRMAP_H 1

#include "name_id.h"
#include "utils.h"

#include <optional>
#include <string_view>

namespace mega {

// maps attribute names to attribute values
struct attr_map : map<nameid, string>
{
    attr_map() {}

    attr_map(nameid key, string value)
    {
        (*this)[key] = std::move(value);
    }

    attr_map(map<nameid, string>&& m)
    {
        m.swap(*this);
    }

    bool contains(nameid k) const
    {
        return this->find(k) != this->end();
    }
};

struct MEGA_API AttrMap
{
    attr_map map;

    bool getBool(const char* name) const;

    /**
     * @brief Returns a string with the value matching the given key if it is found on the map
     */
    std::optional<std::string> getString(const std::string_view name) const;

    /**
     * @brief Returns a view on the value matching the given key if it is found on the map
     */
    std::optional<std::string_view> getStringView(const std::string_view name) const;

    // compute rough storage size
    unsigned storagesize(int) const;

    // convert nameid to string
    static int nameid2string(nameid, char*);
    static string nameid2string(nameid);

    // convert string to nameid
    static nameid string2nameid(const char*);

    static constexpr nameid string2nameid(const std::string_view n)
    {
        return makeNameid(n);
    }

    // export as JSON string
    void getjson(string*) const;
    std::string getjson() const;

    /**
     * @brief generates a string by exporting all [key, value] pairs as a JSON object ({"..."})
     *
     * @example
     * Input:
     *  {
     *      [116] : 20
     *      [28260]: 8
     *      [6384743]: sha256
     *      [1936225125]: abcd
     *  }
     *
     * Output:
     *  "{"t":"20","nd":"8","alg":"sha256","shse":"abcd"}"
     *
     * @return a string by exporting all [key, value] pairs as a JSON object ({"..."})
     */
    std::string getJsonObject() const;

    /**
     * @brief Imports [key, value] pairs in this AttrMap (discarding any existing value) from a
     * string with JSON object format
     * ({"..."})
     *
     * @note: The expected format of input buf must
     * be a well formed JSON string. e.g: "{"t":"20","nd":"8","alg":"sha256","shse":"abcd"}"
     *
     * Also note that this function does not expect nested objects inside another object in that
     * string. e.g: "totp":"{"t":"70", "subObject":"{"a":"1", "b":"2"}"}"
     *
     * @example
     * Input:
     *  "{"t":"20","nd":"8","alg":"sha256","shse":"abcd"}"
     *
     * Result: Stores [k,v] elements in this AttrMap
     *  {
     *      [116] : 20
     *      [28260]: 8
     *      [6384743]: sha256
     *      [1936225125]: abcd
     *  }
     */
    void fromjsonObject(const std::string& buf);

    // import from JSON string
    void fromjson(const char* buf);

    /**
     * @brief If there is an entry in the map with the given name, a new AttrMap is generated
     * assuming the value is in json format.
     */
    std::optional<AttrMap> getNestedJsonObject(const std::string_view name) const;

    /**
     * @brief Generates a new AttrMap from JsonObject String stored in value corresponding to
     * specific key(parentName) of this AttrMap
     *
     * Given a key(parentName), this function searches an object name (childName) in the value of
     * that key (parentName) and extract that object into a new AttrMap
     *
     * @note: The expected format of AttrMap string value that corresponding to key (parentName),
     * must be a well formed JSON string. e.g: "n":"Pasword1
     * notes","u":"myUser","pwd":"abcd","url":"www.website.com","totp":{"t":"70","nd":"8","alg":"sha1","shse":"HVR4CFHAFOWFGGFAGSA5JVTIMMPG6GMT"}
     *
     * Also note that this function does not support nested objects inside another object in that
     * string. e.g: "totp":"{"t":"70", "subObject":"{"a":"1", "b":"2"}"}"
     *
     * @example
     * Input:
     * {
     *      [110] 	  "Pasword1"
     *      [7370605] "n":"Pasword1
     * notes","u":"myUser","pwd":"abcd","url":"www.website.com","totp":{"t":"70","nd":"8","alg":"sha1","shse":"HVR4CFHAFOWFGGFAGSA5JVTIMMPG6GMT"}
     * }
     *
     * Output:
     * {
     *      [1953461360] :
     * {"t":"70","nd":"8","alg":"sha1","shse":"HVR4CFHAFOWFGGFAGSA5JVTIMMPG6GMT"}
     * }
     *
     * In example above we can see that we want to extract "totp" object stored at [7370605]
     *
     * @param parentName key of this AttrMap where we want to extract JSON object
     * @param childName Object name inside Value of parentName key in this AttrMap
     * @return a new AttrMap from JsonObject String stored in value corresponding to
     * specific key(parentName) of this AttrMap
     */
    std::optional<AttrMap> getComplexNestedJsonObject(const std::string_view parentName,
                                                      const std::string_view childName) const;

    // export as raw binary serialize
    void serialize(string*) const;

    // import raw binary serialize
    const char* unserialize(const char*, const char*);

    // overwrite entries in map (or remove them if the value is empty)
    void applyUpdates(const attr_map& updates);

    /**
     * @brief Updates or adds new fields to this object if new values are present in `updates`
     *
     * This method, unlike `applyUpdates`, handles updates on nested fields.
     * @note Just one nesting level is handled.
     *
     * @example
     * This object:
     * {
     *     "a" : "hello",
     *     "b" : "{"foo": "1", "bar": "2"}"
     * }
     * Updating with:
     * {
     *     "a" : "world",
     *     "b" : "{"bar": "3"}"
     * }
     * Final state of this object after this method:
     * {
     *     "a" : "world",
     *     "b" : "{"foo": "1", "bar": "3"}"
     * }
     * Notice the difference if the `applyUpdates` is used instead:
     * {
     *     "a" : "world",
     *     "b" : "{"bar": "3"}"
     * }
     *
     * @param updates The AttrMap with the new values to set in this object
     * @param nestedFieldKeys An array with the keys for fields that have nested objects
     * @note If a dynamic size for the nestedFieldKeys is required, consider changing the type to an
     * std::span if C++20 is available.
     */
    template<size_t S>
    void applyUpdatesWithNestedFields(const AttrMap& updates,
                                      const std::array<std::string_view, S>& nestedFieldKeys)
    {
        const auto getNestedFieldFinalStr =
            [this, &updates](const std::string_view field) -> std::optional<std::string>
        {
            const auto nameId = AttrMap::string2nameid(field);

            if (!hasUpdate(nameId, updates.map) ||
                updates.getStringView(field).value_or("").empty())
                return std::optional<std::string>{std::nullopt};

            AttrMap complexMapUpdatesValues;
            assert(updates.map.contains(nameId)); // otherwise doesn't have updates
            complexMapUpdatesValues.fromjsonObject(updates.map.at(nameId));

            AttrMap complexMapCurrentValues;
            if (this->map.contains(nameId))
            {
                complexMapCurrentValues.fromjsonObject(this->map.at(nameId));
            }

            complexMapCurrentValues.applyUpdates(complexMapUpdatesValues.map);
            return std::make_optional(complexMapCurrentValues.getJsonObject());
        };
        std::array<std::optional<std::string>, S> finalNestedStrs;
        std::transform(begin(nestedFieldKeys),
                       end(nestedFieldKeys),
                       begin(finalNestedStrs),
                       getNestedFieldFinalStr);
        applyUpdates(updates.map);
        // The following could be nicer in c++23 with zip and filter
        for (size_t i = 0; i < nestedFieldKeys.size(); ++i)
            if (auto& finalStr = finalNestedStrs[i]; finalStr.has_value())
                map[AttrMap::string2nameid(nestedFieldKeys[i])] = std::move(finalStr.value());
    }

    // determine if the value of attrId will receive an update if applyUpdates() will be called for updates
    // (an attribute will be updated only if present among received updates;
    // even for removal, it should be present with an empty value)
    bool hasUpdate(nameid attrId, const attr_map& updates) const;

    // determine if attrId differs between the 2 maps
    bool hasDifferentValue(nameid attrId, const attr_map& other) const;

    // remove items from the map where the value is an empty string
    void removeEmptyValues();
};
} // namespace

#endif
