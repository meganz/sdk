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

// Needed for Windows Phone (MSVS 2013 - C++ version 9.8)
#if defined(_WIN32) && _MSC_VER <= 1800 && __cplusplus < 201103L && !defined(_TIMESPEC_DEFINED) && ! __struct_timespec_defined
struct timespec
{
    long long	tv_sec; 	/* seconds */
    long        tv_nsec;	/* nanoseconds */
};
# define __struct_timespec_defined  1
#endif

namespace mega {
// convert 1...8 character ID to int64 integer (endian agnostic)
#define MAKENAMEID1(a) (nameid)(a)
#define MAKENAMEID2(a, b) (nameid)(((a) << 8) + (b))
#define MAKENAMEID3(a, b, c) (nameid)(((a) << 16) + ((b) << 8) + (c))
#define MAKENAMEID4(a, b, c, d) (nameid)(((a) << 24) + ((b) << 16) + ((c) << 8) + (d))
#define MAKENAMEID5(a, b, c, d, e) (nameid)((((uint64_t)a) << 32) + ((b) << 24) + ((c) << 16) + ((d) << 8) + (e))
#define MAKENAMEID6(a, b, c, d, e, f) (nameid)((((uint64_t)a) << 40) + (((uint64_t)b) << 32) + ((c) << 24) + ((d) << 16) + ((e) << 8) + (f))
#define MAKENAMEID7(a, b, c, d, e, f, g) (nameid)((((uint64_t)a) << 48) + (((uint64_t)b) << 40) + (((uint64_t)c) << 32) + ((d) << 24) + ((e) << 16) + ((f) << 8) + (g))
#define MAKENAMEID8(a, b, c, d, e, f, g, h) (nameid)((((uint64_t)a) << 56) + (((uint64_t)b) << 48) + (((uint64_t)c) << 40) + (((uint64_t)d) << 32) + ((e) << 24) + ((f) << 16) + ((g) << 8) + (h))

std::string toNodeHandle(handle nodeHandle);
std::string toHandle(handle h);
#define LOG_NODEHANDLE(x) toNodeHandle(x)
#define LOG_HANDLE(x) toHandle(x)

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
    static void encrypt(PrnGen &rng, string* data, SymmCipher* key, string* iv = NULL);

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

    /**
     * @brief Random blocks generator
     */
    PrnGen &rng;

public:

    /**
     * @brief Constructor. Initializes keys with random values.
     */
    PayCrypter(PrnGen &rng);

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
     * @param key Master key to decrypt the container
     * @return A new TLVstore object. You take the ownership of the object.
     */
    static TLVstore * containerToTLVrecords(const string *data, SymmCipher *key);

    /**
     * @brief Builds a TLV object with records from a container
     * @param data Binary byte array representing the TLV records
     * @return A new TLVstore object. You take the ownership of the object.
     */
    static TLVstore * containerToTLVrecords(const string *data);

    /**
     * @brief Converts the TLV records into an encrypted byte array
     * @param key Master key to decrypt the container
     * @param encSetting Block encryption mode to be used by AES
     * @return A new string holding the encrypted byte array. You take the ownership of the string.
     */
    string *tlvRecordsToContainer(PrnGen &rng, SymmCipher *key, encryptionsetting_t encSetting = AES_GCM_12_16);

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

// for pre-c++11 where this version is not defined yet.  
long long abs(long long n);

extern m_time_t m_time(m_time_t* tt = NULL);
extern struct tm* m_localtime(m_time_t, struct tm *dt);
extern struct tm* m_gmtime(m_time_t, struct tm *dt);
extern m_time_t m_mktime(struct tm*);
extern int m_clock_getmonotonictime(struct timespec *t);
// Similar behaviour to mktime but it receives a struct tm with a date in UTC and return mktime in UTC
extern m_time_t m_mktime_UTC(const struct tm *src);

std::string rfc1123_datetime( time_t time );
std::string webdavurlescape(const std::string &value);
std::string escapewebdavchar(const char c);
std::string webdavnameescape(const std::string &value);

void tolower_string(std::string& str);


struct CacheableWriter
{
    CacheableWriter(string& d);
    string& dest;

    void serializebinary(byte* data, size_t len);
    void serializecstr(const char* field, bool storeNull);  // may store the '\0' also for backward compatibility
    void serializestring(const string& field);
    void serializei64(int64_t field);
    void serializeu32(uint32_t field);
    void serializehandle(handle field);
    void serializebool(bool field);
    void serializebyte(byte field);
    void serializedouble(double field);
    void serializechunkmacs(const chunkmac_map& m);

    // Each class that might get extended should store expansion flags at the end
    // When adding new fields to an existing class, set the next expansion flag true to indicate they are present.
    // If you turn on the last flag, then you must also add another set of expansion flags (all false) after the new fields, for further expansion later.
    void serializeexpansionflags(bool b1 = false, bool b2 = false, bool b3 = false, bool b4 = false, bool b5 = false, bool b6 = false, bool b7 = false, bool b8 = false);
};

struct CacheableReader
{
    CacheableReader(const string& d);
    const char* ptr;
    const char* end;
    unsigned fieldnum;

    bool unserializebinary(byte* data, size_t len);
    bool unserializecstr(string& s, bool removeNull); // set removeNull if this field stores the terminating '\0' at the end
    bool unserializestring(string& s);
    bool unserializei64(int64_t& s);
    bool unserializeu32(uint32_t& s);
    bool unserializebyte(byte& s);
    bool unserializedouble(double& s);
    bool unserializehandle(handle& s);
    bool unserializebool(bool& s);
    bool unserializechunkmacs(chunkmac_map& m);

    bool unserializeexpansionflags(unsigned char field[8], unsigned usedFlagCount);

    void eraseused(string& d); // must be the same string, unchanged
};

template<typename T>
void hashCombine(size_t& seed, const T& v)
{
    // Taken from Boost's hash combine function
    seed ^= std::hash<T>{}(v) + 0x9e3779b9 + (seed<<6) + (seed>>2);
}

/////// Following are a few helpers that are required for compile-time `forEach` further down

template<std::size_t...>
struct Indices
{};

template<std::size_t...>
struct ConstructRange;

template<std::size_t end, std::size_t idx, std::size_t... i>
struct ConstructRange<end, idx, i...> : ConstructRange<end, idx + 1, i..., idx>
{};

template<std::size_t end, std::size_t... i>
struct ConstructRange<end, end, i...>
{
    using type = Indices<i...>;
};

template<std::size_t b, std::size_t e>
struct IndexRange
{
    using type = typename ConstructRange<e, b>::type;
};

template<typename Container, typename Functor>
void forEachIndex(Indices<>, Container&&, Functor&&)
{}

template<std::size_t i, std::size_t... j, typename Container, typename Functor>
void forEachIndex(Indices<i, j...>, Container&& container, Functor&& functor)
{
    std::forward<Functor>(functor)(std::get<i>(std::forward<Container>(container)));
    forEachIndex(Indices<j...>{}, std::forward<Container>(container), std::forward<Functor>(functor));
}

/////// forEach over a std::tuple, unrolled at compile time

template<typename Tuple, typename Functor>
void forEach(Tuple&& tup, Functor&& functor)
{
    constexpr auto size = std::tuple_size<typename std::decay<Tuple>::type>::value;
    using IndexType = typename IndexRange<0, size>::type;
    forEachIndex(IndexType{}, std::forward<Tuple>(tup), std::forward<Functor>(functor));
}

} // namespace

#endif
