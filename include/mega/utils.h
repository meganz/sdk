/**
 * @file mega/utils.h
 * @brief Mega SDK various utilities and helper classes
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

#ifndef MEGA_UTILS_H
#define MEGA_UTILS_H 1

#include "types.h"
#include "mega/logging.h"

namespace mega {
// convert 2...8 character ID to int64 integer (endian agnostic)
#define MAKENAMEID2(a, b) (nameid)(((a) << 8) + (b))
#define MAKENAMEID3(a, b, c) (nameid)(((a) << 16) + ((b) << 8) + (c))
#define MAKENAMEID4(a, b, c, d) (nameid)(((a) << 24) + ((b) << 16) + ((c) << 8) + (d))
#define MAKENAMEID5(a, b, c, d, e) (nameid)((((uint64_t)a) << 32) + ((b) << 24) + ((c) << 16) + ((d) << 8) + (e))
#define MAKENAMEID6(a, b, c, d, e, f) (nameid)((((uint64_t)a) << 40) + (((uint64_t)b) << 32) + ((c) << 24) + ((d) << 16) + ((e) << 8) + (f))
#define MAKENAMEID7(a, b, c, d, e, f, g) (nameid)((((uint64_t)a) << 48) + (((uint64_t)b) << 40) + (((uint64_t)c) << 32) + ((d) << 24) + ((e) << 16) + ((f) << 8) + (g))
#define MAKENAMEID8(a, b, c, d, e, f, g, h) (nameid)((((uint64_t)a) << 56) + (((uint64_t)b) << 48) + (((uint64_t)c) << 40) + (((uint64_t)d) << 32) + ((e) << 24) + ((f) << 16) + ((g) << 8) + (h))

std::string toNodeHandle(handle nodeHandle);
#define LOG_NODEHANDLE(x) toNodeHandle(x)

struct MEGA_API ChunkedHash
{
    static const int SEGSIZE = 131072;

    static m_off_t chunkfloor(m_off_t);
    static m_off_t chunkceil(m_off_t, m_off_t limit = -1);
};

/**
 * @brief Padded encryption using AES-128 in CBC mode.
 */
struct MEGA_API PaddedCBC
{
    /**
     * @brief Encrypts a string after padding it to block length.
     *
     * Note: With an IV, only use the first 8 bytes.
     *
     * @param data Data buffer to be encrypted. Encryption is done in-place,
     *     so cipher text will be in `data` afterwards as well.
     * @param key AES key for encryption.
     * @param iv Optional initialisation vector for encryption. Will use a
     *     zero IV if not given. If `iv` is a zero length string, a new IV
     *     for encryption will be generated and available through the reference.
     * @return Void.
     */
    static void encrypt(string* data, SymmCipher* key, string* iv = NULL);

    /**
     * @brief Decrypts a string and strips the padding.
     *
     * Note: With an IV, only use the first 8 bytes.
     *
     * @param data Data buffer to be decrypted. Decryption is done in-place,
     *     so plain text will be in `data` afterwards as well.
     * @param key AES key for decryption.
     * @param iv Optional initialisation vector for encryption. Will use a
     *     zero IV if not given.
     * @return Void.
     */
    static bool decrypt(string* data, SymmCipher* key, string* iv = NULL);
};

class MEGA_API HashSignature
{
    Hash* hash;

public:
    // add data
    void add(const byte*, unsigned);

    // generate signature
    unsigned get(AsymmCipher*, byte*, unsigned);

    // verify signature
    bool checksignature(AsymmCipher*, const byte*, unsigned);

    HashSignature(Hash*);
    ~HashSignature();
};

/**
 * @brief Crypto functions related to payments
 */
class MEGA_API PayCrypter
{
    /**
     * @brief Length of the AES key
     */
    static const int ENC_KEY_BYTES = 16;

    /**
     * @brief Lenght of the key to generate the HMAC
     */
    static const int MAC_KEY_BYTES = 32;

    /**
     * @brief Length of the IV for AES-CBC
     */
    static const int IV_BYTES = 16;

    /**
     * @brief Buffer for the AES key and the HMAC key
     */
    byte keys[ENC_KEY_BYTES+MAC_KEY_BYTES];

    /**
     * @brief Pointer to the buffer with the AES key
     */
    byte *encKey;

    /**
     * @brief Pointer to the buffer with the HMAC key
     */
    byte *hmacKey;

    /**
     * @brief Buffer with the IV for AES-CBC
     */
    byte iv[IV_BYTES];

public:

    /**
     * @brief Constructor. Initializes keys with random values.
     */
    PayCrypter();

    /**
     * @brief Updates the crypto keys (mainly for testing)
     * @param newEncKey New AES key (must contain ENC_KEY_BYTES bytes)
     * @param newHmacKey New HMAC key (must contain MAC_KEY_BYTES bytes)
     * @param newIv New IV for AES-CBC (must contain IV_BYTES bytes)
     */
    void setKeys(const byte *newEncKey, const byte *newHmacKey, const byte *newIv);

    /**
     * @brief Encrypts the cleartext and returns the payload string.
     *
     * The clear text is encrypted with AES-CBC, then a HMAC-SHA256 is generated for (IV + ciphertext)
     * and finally returns (HMAC + IV + ciphertext)
     *
     * @param cleartext Clear text to generate the payload
     * @param result The function will fill this string with the generated payload
     * @return True if the funcion succeeds, otherwise false
     */
    bool encryptPayload(const string *cleartext, string *result);

    /**
     * @brief Encrypts the cleartext using RSA with random padding.
     *
     * A 2-byte header is inserted just before the clear text with the size in bytes.
     * The result is padded with random bytes. Then RSA is applied and the result is returned
     * in the third parameter, with a 2-byte header that contains the size of the result of RSA.
     *
     * @param cleartext Clear text to encrypt with RSA
     * @param pubkdata Public key in binary format (result of AsymmCipher::serializekey)
     * @param pubkdatalen Size (in bytes) of pubkdata
     * @param result RSA encrypted text, with a 2-byte header with the size of the RSA buffer in bytes
     * @param randompadding Enables padding with random bytes. Otherwise, the cleartext is 0-padded
     * @return True if the funcion succeeds, otherwise false
     */
    bool rsaEncryptKeys(const string *cleartext, const byte *pubkdata, int pubkdatalen, string *result, bool randompadding = true);

    /**
     * @brief Encrypts clear text data to an authenticated ciphertext, authenticated with an HMAC.
     * @param cleartext Clear text as byte string
     * @param pubkdata Public key in binary format (result of AsymmCipher::serializekey)
     * @param pubkdatalen Size (in bytes) of pubkdata
     * @param result Encrypted data block as byte string.
     * @param randompadding Enables padding with random bytes. Otherwise, the cleartext is 0-padded
     * @return True if the funcion succeeds, otherwise false
     */
    bool hybridEncrypt(const string *cleartext, const byte *pubkdata, int pubkdatalen, string *result, bool randompadding = true);
};

// read/write multibyte words
struct MEGA_API MemAccess
{
#ifndef ALLOW_UNALIGNED_MEMORY_ACCESS
    template<typename T> static T get(const char* ptr)
    {
        T val;
        memcpy(&val,ptr,sizeof(T));
        return val;
    }

    template<typename T> static void set(byte* ptr, T val)
    {
        memcpy(ptr,&val,sizeof val);
    }
#else
    template<typename T> static T get(const char* ptr)
    {
        return *(T*)ptr;
    }

    template<typename T> static void set(byte* ptr, T val)
    {
        *(T*)ptr = val;
    }
#endif
};

#ifdef _WIN32
int mega_snprintf(char *s, size_t n, const char *format, ...);
#endif

struct MEGA_API TLVstore
{
private:
    TLV_map tlv;

 public:

    /**
     * @brief containerToTLVrecords Builds a TLV object with records from an encrypted container
     * @param data Binary byte array representing the encrypted container
     * @param datalen Length of the byte array.
     * @param key Master key to decrypt the container
     * @return A new TLVstore object. You take the ownership of the object.
     */
    static TLVstore * containerToTLVrecords(const string *data, SymmCipher *key);

    /**
     * @brief Builds a TLV object with records from a container
     * @param data Binary byte array representing the TLV records
     * @param datalen Length of the byte array.
     * @return A new TLVstore object. You take the ownership of the object.
     */
    static TLVstore * containerToTLVrecords(const string *data);

    /**
     * @brief Converts the TLV records into an encrypted byte array
     * @param key Master key to decrypt the container
     * @param encSetting Block encryption mode to be used by AES
     * @return A new string holding the encrypted byte array. You take the ownership of the string.
     */
    string *tlvRecordsToContainer(SymmCipher *key, encryptionsetting_t encSetting = AES_GCM_12_16);

    /**
     * @brief Converts the TLV records into a byte array
     * @return A new string holding the byte array. You take the ownership of the string.
     */
    string *tlvRecordsToContainer();

    /**
     * @brief get Get the value for a given key
     * @param type Type of the value (without scope nor non-historic modifiers).
     * @return String containing the array with the value, or NULL if error.
     */
    string get(string type);

    /**
     * @brief Get a reference to the TLV_map associated to this TLVstore
     *
     * The TLVstore object retains the ownership of the returned object. It will be
     * valid until this TLVstore object is deleted.
     *
     * @return The TLV_map associated to this TLVstore
     */
    const TLV_map *getMap() const;

    /**
     * @brief Get a list of the keys contained in the TLV
     *
     * You take ownership of the returned value.
     *
     * @return A new vector with the keys included in the TLV
     */
    vector<string> *getKeys() const;

    /**
     * @brief find Checks whether a type of value is available in the TLV container.
     * @param type Type of the value (without scope nor non-historic modifiers).
     * @return True if the type of value is found, false otherwise.
     */
    bool find(string type);

    /**
     * @brief add Adds a new record to the container
     * @param type Type for the new value (without scope nor non-historic modifiers).
     * @param value New value to be set.
     * @return
     */
    void set(string type, string value);

    size_t size();

    static unsigned getTaglen(int mode);
    static unsigned getIvlen(int mode);
    static encryptionmode_t getMode(int mode);

    ~TLVstore();
};

class Utils {
public:
    /**
     * @brief Converts a character string from UTF-8 to Unicode
     * This method is a workaround for a legacy bug where Webclient used to encode
     * each byte of the array in UTF-8, resulting in a wider string of variable length.
     * @note The UTF-8 string should only contain characters encoded as 1 or 2 bytes.
     * @param src Characters string encoded in UTF-8
     * @param srclen Length of the string (in bytes)
     * @param result String holding the byte array of Unicode characters
     * @return True if success, false if the byte 'src' is not a valid UTF-8 string
     */
    static bool utf8toUnicode(const uint8_t *src, unsigned srclen, string *result);
};


} // namespace

#endif
