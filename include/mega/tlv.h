#ifndef MEGA_TLV_H
#define MEGA_TLV_H

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace mega
{

class PrnGen;
class SymmCipher;
using TLV_map = std::map<std::string, std::string>;

enum encryptionsetting_t
{
    AES_CCM_12_16 = 0x00,
    AES_CCM_10_16 = 0x01,
    AES_CCM_10_08 = 0x02,
    AES_GCM_12_16_BROKEN = 0x03, // Same as 0x00 (due to a legacy bug)
    AES_GCM_10_08_BROKEN = 0x04, // Same as 0x02 (due to a legacy bug)
    AES_GCM_12_16 = 0x10,
    AES_GCM_10_08 = 0x11
};

enum encryptionmode_t
{
    AES_MODE_UNKNOWN,
    AES_MODE_CCM,
    AES_MODE_GCM
};

class TLVstore
{
public:
    static std::unique_ptr<std::string> recordsToContainer(TLV_map&& records,
                                                           PrnGen& rng,
                                                           SymmCipher& key);

    /**
     * @brief Build a TLV object with records from an encrypted container
     *
     * @param data Binary byte array representing the encrypted container
     * @param key Master key to decrypt the container
     *
     * @return A new TLVstore object. You take the ownership of the object.
     */
    static TLVstore* containerToTLVrecords(const std::string* data, SymmCipher* key);

    /**
     * @brief Build a TLV object with records from a container
     *
     * @param data Binary byte array representing the TLV records
     *
     * @return A new TLVstore object. You take the ownership of the object.
     *
     * @note: Still public method because it's used by MEGAchat implementation
     */
    static TLVstore* containerToTLVrecords(const std::string* data);

    /**
     * @brief Convert the TLV records into an encrypted byte array
     *
     * @param key Master key to encrypt the container
     * @param encSetting Block encryption mode to be used by AES
     *
     * @return A new string holding the encrypted byte array. You take the ownership of the string.
     */
    std::string* tlvRecordsToContainer(PrnGen& rng,
                                       SymmCipher* key,
                                       encryptionsetting_t encSetting = AES_GCM_12_16);

    /**
     * @brief Convert the TLV records into a byte array
     *
     * @return A new string holding the byte array. You take the ownership of the string.
     *
     * @note: Still public method because it's used by MEGAchat implementation
     */
    std::string* tlvRecordsToContainer();

    /**
     * @brief get Get the value for a given key
     *
     * In case the type is found, it will update value parameter and return true.
     * In case the type is not found, it will return false and value will not be changed.
     *
     * @param type Type of the value (without scope nor non-historic modifiers).
     * @param value Set to corresponding value if type was found.
     *
     * @return True if type was found, false otherwise.
     */
    bool get(const std::string& type, std::string& value) const;

    /**
     * @brief Get a reference to the TLV_map associated to this TLVstore
     *
     * The TLVstore object retains the ownership of the returned object. It will be
     * valid until this TLVstore object is deleted.
     *
     * @return The TLV_map associated to this TLVstore
     */
    const TLV_map* getMap() const;

    /**
     * @brief Get a list of the keys contained in the TLV
     *
     * You take ownership of the returned value.
     *
     * @return A new vector with the keys included in the TLV
     */
    std::vector<std::string>* getKeys() const;

    /**
     * @brief Add a new record to the container
     *
     * @param type Type for the new value (without scope nor non-historic modifiers).
     * @param value New value to be set.
     */
    void set(const std::string& type, const std::string& value);

    /**
     * @brief Replace all records in the container
     *
     * @param records New records to be set.
     */
    void set(TLV_map&& records);

    /**
     * @brief Remove a record from the container
     *
     * @param type Type for the value to be removed (without scope nor non-historic modifiers).
     */
    void reset(const std::string& type);

    size_t size() const;

private:
    static unsigned getTaglen(int mode);
    static unsigned getIvlen(int mode);
    static encryptionmode_t getMode(int mode);

    TLV_map tlv;
};

} // namespace mega

#endif // MEGA_TLV_H
