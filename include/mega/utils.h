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

#include <charconv>
#include <condition_variable>
#include <mutex>
#include <shared_mutex>
#include <thread>
#include <type_traits>
#undef SSIZE_MAX
#include "mega/mega_utf8proc.h"
#undef SSIZE_MAX

// Include ICU headers
#include <unicode/uchar.h>

namespace mega {
std::string toNodeHandle(handle nodeHandle);
std::string toNodeHandle(NodeHandle nodeHandle);
NodeHandle toNodeHandle(const byte* data);  // consider moving functionality to NodeHandle
NodeHandle toNodeHandle(const std::string* data);
std::string toHandle(handle h);
std::pair<bool, TypeOfLink> toTypeOfLink (nodetype_t type);
#define LOG_NODEHANDLE(x) toNodeHandle(x)
#define LOG_HANDLE(x) toHandle(x)
class SimpleLogger;
class LocalPath;
SimpleLogger& operator<<(SimpleLogger&, NodeHandle h);
SimpleLogger& operator<<(SimpleLogger&, UploadHandle h);
SimpleLogger& operator<<(SimpleLogger&, NodeOrUploadHandle h);
SimpleLogger& operator<<(SimpleLogger& s, const LocalPath& lp);

typedef enum
{
    FORMAT_SCHEDULED_COPY = 0,  // 20221205123045
    FORMAT_ISO8601        = 1,  // 20221205T123045
} date_time_format_t;

std::string backupTypeToStr(BackupType type);

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
     * @return true if encryption was successful.
     */
    static bool encrypt(PrnGen &rng, string* data, SymmCipher* key, string* iv = NULL);

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
     * @return true if decryption was successful.
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
// get the Windows error message in UTF-8
std::string winErrorMessage(DWORD error);

#endif

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

    /**
     * @brief Determines size in bytes of a valid UTF-8 sequence.
     * @param c first character of UTF-8 sequence
     * @return the size of UTF-8 sequence if its valid, otherwise returns 0
     */
    static size_t utf8SequenceSize(unsigned char c);

    /**
     * @brief This function is analogous to a32_to_str in js version.
     * Converts a vector of <T> elements into a std::string
     *
     * @param data a vector of <T> elements
     * @note this function has been initially designed to work with <T> = uint32_t or <T> = int32_t
     * This is a valid example: <t> = uint32_t, data = [1952805748] => return_value = "test"
     *
     * @return returns a std::string
     */
    template<typename T> static std::string a32_to_str(std::vector<T> data)
    {
        size_t size = data.size() * sizeof(T);
        std::unique_ptr<char[]> result(new char[size]);
        for (size_t i = 0; i < size; ++i)
        {
            result[i] = static_cast<char>((data[i >> 2] >> (24 - (i & 3) * 8)) & 255);
        }
        return std::string (result.get(), size);
    }

    /**
     * @brief This function is analogous to str_to_a32 in js version.
     * Converts a std::string into a vector of <T> elements
     *
     * @param data a std::string
     * @note this function has been initially designed to work with <T> = uint32_t or <T> = int32_t
     * This is a valid example: <t> = uint32_t, data = "test"  => return_value = [1952805748]
     *
     * @return returns a vector of <T> elements
     */
    template<typename T> static std::vector<T> str_to_a32(std::string data)
    {
        std::vector<T> data32((data.size() + 3) >> 2);
        for (size_t i = 0; i < data.size(); ++i)
        {
            data32[i >> 2] |= (data[i] & 255) << (24 - (i & 3) * 8);
        }
        return data32;
    }

    static std::string stringToHex(const std::string& input);
    static std::string hexToString(const std::string& input);
    /**
     * @brief Converts a hexadecimal string to a uint64_t value. The input string may or may not have the hex prefix "0x".
     *
     * @param input The hexadecimal string to be converted (ex: "78b1bbbda5f32526", "0x10FF, "0x0001")
     * @return The uint64_t value corresponding to the input hexadecimal string.
    */
    static uint64_t hexStringToUint64(const std::string &input);
    /**
     * @brief Converts a 8-byte numeric value to a 16-character lowercase hexadecimal string, with zero-padding if necessary.
     *
     * @param input The uint64_t value to be converted to a hexadecimal string.
     * @return A 16-character lowercase hexadecimal string representation of the input value (ex: "78b1bbbda5f32526").
     *
    */
    static std::string uint64ToHexString(uint64_t input);

    static int32_t toLower(const int32_t c)
    {
        return utf8proc_tolower(c);
    }

    static int32_t toUpper(const int32_t c)
    {
        return utf8proc_toupper(c);
    }

    static string toUpperUtf8(const string& text);
    static string toLowerUtf8(const string& text);

    // Platform-independent case-insensitive comparison.
    static int icasecmp(const std::string& lhs,
                        const std::string& rhs,
                        const size_t length);

    static int icasecmp(const std::wstring& lhs,
                        const std::wstring& rhs,
                        const size_t length);

    // Same as above but only case-insensitive on Windows.
    static int pcasecmp(const std::string& lhs,
                        const std::string& rhs,
                        const size_t length);

    static int pcasecmp(const std::wstring& lhs,
                        const std::wstring& rhs,
                        const size_t length);

    static std::string replace(const std::string& str,
                               char search,
                               char replace);
    static std::string replace(const std::string& str,
                               const std::string& search,
                               const std::string& replacement);

    // join({"a", "new", "loom"}, "; ") -> "a; new; loom"
    static std::string join(const std::vector<std::string>& items, const std::string& with);
    static bool startswith(const std::string& str, const std::string& start);
    static bool startswith(const std::string& str, char chr);
    static bool endswith(const std::string& str, char chr);
    static const std::string _trimDefaultChars;
    // return string with trimchrs removed from front and back of given string str
    static string trim(const string& str, const string& trimchars = _trimDefaultChars);


    // --- environment functions that work with Unicode UTF-8 on Windows (set/unset/get) ---

    static bool hasenv(const std::string& key);
    static std::pair<std::string, bool> getenv(const std::string& key);
    // return def if value not found
    static std::string getenv(const std::string& key, const std::string& def);
    static void setenv(const std::string& key, const std::string& value);
    static void unsetenv(const std::string& key);
};

extern m_time_t m_time(m_time_t* tt = NULL);
extern struct tm* m_localtime(m_time_t, struct tm *dt);
extern struct tm* m_gmtime(m_time_t, struct tm *dt);
extern m_time_t m_mktime(struct tm*);
extern dstime m_clock_getmonotonictimeDS();
// Similar behaviour to mktime but it receives a struct tm with a date in UTC and return mktime in UTC
extern m_time_t m_mktime_UTC(const struct tm *src);

/**
 * Converts a datetime from string format into a Unix timestamp
 * Allowed input formats:
 *  + FORMAT_SCHEDULED_COPY  => 20221205123045   => output format: Unix timestamp in deciseconds
 *  + FORMAT_ISO8601         => 20221205T123045  => output format: Unix timestamp in seconds
*/
extern time_t stringToTimestamp(string stime, date_time_format_t format);

std::string rfc1123_datetime( time_t time );
std::string webdavurlescape(const std::string &value);
std::string escapewebdavchar(const char c);
std::string webdavnameescape(const std::string &value);

void tolower_string(std::string& str);

#ifdef __APPLE__
int macOSmajorVersion();
#endif

// file chunk macs
class chunkmac_map
{

    struct ChunkMAC
    {
        // do not change the size or layout, it is directly serialized to db from whatever the binary format is for this compiler/platform
        byte mac[SymmCipher::BLOCKSIZE];

        // For a partially completed chunk, offset is the number of bytes processed (from the start of the chunk)
        // For a finished chunk, it's 0
        // When we start consolidating from the front for macsmac calculation, it's -1 (and finished==true)
        unsigned int offset = 0;

        // True when the entire chunk has been processed.
        // For the special case of the first record being the macsmac calculation to this point,
        // finished == true and offset == -1, and mac == macsmac to the end of this block.
        bool finished = false;

        // True when the chunk is not entirely processed.
        // Offset is only increased by downloads, so (!offset) should always be true for uploads.
        bool notStarted() { return !finished && !offset; }

        // the very first record can be the macsmac calculation so far, from the start to some contiguous point
        bool isMacsmacSoFar() { return finished && offset == unsigned(-1); }
    };

    map<m_off_t, ChunkMAC> mMacMap;

    // we collapse the leading consecutive entries, for large files.
    // this is the map key for how far that collapsing has progressed
    m_off_t macsmacSoFarPos = -1;

    m_off_t progresscontiguous = 0;


public:
    int64_t macsmac(SymmCipher *cipher);
    int64_t macsmac_gaps(SymmCipher *cipher, size_t g1, size_t g2, size_t g3, size_t g4);
    void serialize(string& d) const;
    bool unserialize(const char*& ptr, const char* end);
    void calcprogress(m_off_t size, m_off_t& chunkpos, m_off_t& completedprogress, m_off_t* sumOfPartialChunks = nullptr);
    m_off_t nextUnprocessedPosFrom(m_off_t pos);
    m_off_t expandUnprocessedPiece(m_off_t pos, m_off_t npos, m_off_t fileSize, m_off_t maxReqSize);
    m_off_t hasUnfinishedGap(m_off_t fileSize);
    void finishedUploadChunks(chunkmac_map& macs);
    bool finishedAt(m_off_t pos);
    m_off_t updateContiguousProgress(m_off_t fileSize);
    void updateMacsmacProgress(SymmCipher *cipher);
    void copyEntriesTo(chunkmac_map& other);
    void copyEntryTo(m_off_t pos, chunkmac_map& other);
    void debugLogOuputMacs();

    void ctr_encrypt(m_off_t chunkid, SymmCipher *cipher, byte *chunkstart, unsigned chunksize, m_off_t startpos, int64_t ctriv, bool finishesChunk);
    void ctr_decrypt(m_off_t chunkid, SymmCipher *cipher, byte *chunkstart, unsigned chunksize, m_off_t startpos, int64_t ctriv, bool finishesChunk);

    size_t size() const
    {
        return mMacMap.size();
    }
    void clear()
    {
        mMacMap.clear();
        macsmacSoFarPos = -1;
        progresscontiguous = 0;
    }
    void swap(chunkmac_map& other) {
        mMacMap.swap(other.mMacMap);
        std::swap(macsmacSoFarPos, other.macsmacSoFarPos);
        std::swap(progresscontiguous, other.progresscontiguous);
    }
};

struct CacheableWriter
{
    CacheableWriter(string& d);
    string& dest;

    void serializebinary(byte* data, size_t len);
    void serializecstr(const char* field, bool storeNull);  // may store the '\0' also for backward compatibility. Only use for utf8!  (std::string storing double byte chars will only store 1 byte)
    void serializepstr(const string* field);  // uses string size() not strlen
    void serializestring(const string& field);
    void serializestring_u32(const string& field); // use uint32_t for the size field
    void serializecompressedu64(uint64_t field);
    void serializecompressedi64(int64_t field) { serializecompressedu64(static_cast<uint64_t>(field)); }

    // DO NOT add size_t or other types that are different sizes in different builds, eg 32/64 bit compilation
    void serializei8(int8_t field);
    void serializei32(int32_t field);
    void serializei64(int64_t field);
    void serializeu64(uint64_t field);
    void serializeu32(uint32_t field);
    void serializeu16(uint16_t field);
    void serializeu8(uint8_t field);
    void serializehandle(handle field);
    void serializenodehandle(handle field);
    void serializeNodeHandle(NodeHandle field);
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
    bool unserializestring_u32(string& s);
    bool unserializecompressedu64(uint64_t& field);
    bool unserializecompressedi64(int64_t& field) { return unserializecompressedu64(reinterpret_cast<uint64_t&>(field)); }

    // DO NOT add size_t or other types that are different sizes in different builds, eg 32/64 bit compilation
    bool unserializei8(int8_t& s);
    bool unserializei32(int32_t& s);
    bool unserializei64(int64_t& s);
    bool unserializeu16(uint16_t &s);
    bool unserializeu32(uint32_t& s);
    bool unserializeu8(uint8_t& field);
    bool unserializeu64(uint64_t& s);
    bool unserializebyte(byte& s);
    bool unserializedouble(double& s);
    bool unserializehandle(handle& s);
    bool unserializenodehandle(handle& s);
    bool unserializeNodeHandle(NodeHandle& s);
    bool unserializebool(bool& s);
    bool unserializechunkmacs(chunkmac_map& m);
    bool unserializefingerprint(FileFingerprint& fp);
    bool unserializedirection(direction_t& field);  // historic; size varies by compiler.  todo: Remove when we next roll the transfer db version

    bool unserializeexpansionflags(unsigned char field[8], unsigned usedFlagCount);

    void eraseused(string& d); // must be the same string, unchanged
    bool hasdataleft() { return end > ptr; }
};

struct FileAccess;
struct InputStreamAccess;
class SymmCipher;

std::pair<bool, int64_t> generateMetaMac(SymmCipher &cipher, FileAccess &ifAccess, const int64_t iv);

std::pair<bool, int64_t> generateMetaMac(SymmCipher &cipher, InputStreamAccess &isAccess, const int64_t iv);

bool CompareLocalFileMetaMacWithNodeKey(FileAccess* fa, const std::string& nodeKey, int type);

bool CompareLocalFileMetaMacWithNode(FileAccess* fa, Node* node);

// Helper class for MegaClient.  Suitable for expansion/templatizing for other use caes.
// Maintains a small thread pool for executing independent operations such as encrypt/decrypt a block of data
// The number of threads can be 0 (eg. for helper MegaApi that deals with public folder links) in which case something queued is
// immediately executed synchronously on the caller's thread
struct MegaClientAsyncQueue
{
    void push(std::function<void(SymmCipher&)> f, bool discardable);
    void clearDiscardable();

    MegaClientAsyncQueue(Waiter& w, unsigned threadCount);
    ~MegaClientAsyncQueue();

private:
    Waiter& mWaiter;
    std::mutex mMutex;
    std::condition_variable mConditionVariable;

    struct Entry
    {
        bool discardable = false;
        std::function<void(SymmCipher&)> f;
        Entry(bool disc, std::function<void(SymmCipher&)>&& func)
             : discardable(disc), f(func)
        {}
    };

    std::deque<Entry> mQueue;
    std::vector<std::thread> mThreads;
    SymmCipher mZeroThreadsCipher;

    void asyncThreadLoop();
};

template<class T>
struct ThreadSafeDeque
{
    // Just like a deque, but thread safe so that a separate thread can receive filesystem notifications as soon as they are available.
    // When we try to do that on the same thread, the processing of queued notifications is too slow so more notifications bulid up than
    // have been processed, so each time we get the outstanding ones from the buffer we gave to the OS, we need to give it an even
    // larger buffer to write into, otherwise it runs out of space before this thread is idle and can get the next batch from the buffer.
protected:
    std::deque<T> mNotifications;
    std::mutex m;

public:

    bool peekFront(T& t)
    {
        std::lock_guard<std::mutex> g(m);
        if (!mNotifications.empty())
        {
            t = mNotifications.front();
            return true;
        }
        return false;
    }

    bool popFront(T& t)
    {
        std::lock_guard<std::mutex> g(m);
        if (!mNotifications.empty())
        {
            t = std::move(mNotifications.front());
            mNotifications.pop_front();
            return true;
        }
        return false;
    }

    void unpopFront(const T& t)
    {
        std::lock_guard<std::mutex> g(m);
        mNotifications.push_front(t);
    }

    void pushBack(T&& t)
    {
        std::lock_guard<std::mutex> g(m);
        mNotifications.push_back(t);
    }

    bool empty()
    {
        std::lock_guard<std::mutex> g(m);
        return mNotifications.empty();
    }

    size_t size()
    {
        std::lock_guard<std::mutex> g(m);
        return mNotifications.size();
    }

};

template<class K, class V>
class ThreadSafeKeyValue
{
    // This is a thread-safe key-value container restricted to accepting only numeric values.
    // Only the needed interfaces were implemented. Add new ones as they become useful.
public:
    std::unique_ptr<V> get(const K& key) const
    {
        std::shared_lock lock(mMutex);
        auto it = mStorage.find(key);
        return it == mStorage.end() ? nullptr : std::make_unique<V>(it->second);
    }

    void set(const K& key, const V& value)
    {
        std::unique_lock lock(mMutex);
        mStorage[key] = value;
    }

    void clear()
    {
        std::unique_lock lock(mMutex);
        return mStorage.clear();
    }

private:
    mutable std::shared_mutex mMutex;
    std::map<K, V> mStorage;
};

template<typename CharT>
struct UnicodeCodepointIteratorTraits;

template<>
struct UnicodeCodepointIteratorTraits<char>
{
    static ptrdiff_t get(int32_t& codepoint, const char* m, const char* n)
    {
        assert(m && n && m < n);

        return utf8proc_iterate(reinterpret_cast<const uint8_t*>(m),
                                n - m,
                                &codepoint);
    }

    static size_t length(const char* s)
    {
        assert(s);

        return strlen(s);
    }
}; // UnicodeCodepointIteratorTraits<char>

template<>
struct UnicodeCodepointIteratorTraits<wchar_t>
{
    static ptrdiff_t get(int32_t& codepoint, const wchar_t* m, const wchar_t* n)
    {
        assert(m && n && m < n);

        // Are we looking at a high surrogate?
        if ((*m >> 10) == 0x36)
        {
            // Is it followed by a low surrogate?
            if (n - m < 2 || (m[1] >> 10) != 0x37)
            {
                // Nope, the string is malformed.
                return -1;
            }

            // Compute addend.
            const int32_t lo = m[1] & 0x3ff;
            const int32_t hi = *m & 0x3ff;
            const int32_t addend = (hi << 10) | lo;

            // Store effective code point.
            codepoint = 0x10000 + addend;

            return 2;
        }

        // Are we looking at a low surrogate?
        if ((*m >> 10) == 0x37)
        {
            // Then the string is malformed.
            return -1;
        }

        // Code point is encoded by a single code unit.
        codepoint = *m;

        return 1;
    }

    static size_t length(const wchar_t* s)
    {
        assert(s);

        return wcslen(s);
    }
}; // UnicodeCodepointIteratorTraits<wchar_t>

template<typename CharT>
class UnicodeCodepointIterator
{
public:
    using traits_type = UnicodeCodepointIteratorTraits<CharT>;

    UnicodeCodepointIterator(const CharT* s, size_t length)
      : mCurrent(s)
      , mEnd(s + length)
    {
    }

    explicit UnicodeCodepointIterator(const std::basic_string<CharT>& s)
      : UnicodeCodepointIterator(s.data(), s.size())
    {
    }

    explicit UnicodeCodepointIterator(const CharT* s)
      : UnicodeCodepointIterator(s, traits_type::length(s))
    {
    }

    UnicodeCodepointIterator(const UnicodeCodepointIterator& other)
      : mCurrent(other.mCurrent)
      , mEnd(other.mEnd)
    {
    }

    UnicodeCodepointIterator()
      : mCurrent(nullptr)
      , mEnd(nullptr)
    {
    }

    UnicodeCodepointIterator& operator=(const UnicodeCodepointIterator& rhs)
    {
        if (this != &rhs)
        {
            mCurrent = rhs.mCurrent;
            mEnd = rhs.mEnd;
        }

        return *this;
    }

    bool operator==(const UnicodeCodepointIterator& rhs) const
    {
        return mCurrent == rhs.mCurrent && mEnd == rhs.mEnd;
    }

    bool operator!=(const UnicodeCodepointIterator& rhs) const
    {
        return !(*this == rhs);
    }

    bool end() const
    {
        return mCurrent == mEnd;
    }

    int32_t get()
    {
        int32_t result = 0;

        if (mCurrent < mEnd)
        {
            ptrdiff_t nConsumed = traits_type::get(result, mCurrent, mEnd);
            assert(nConsumed > 0);
            mCurrent += nConsumed;
        }

        return result;
    }

    bool match(const int32_t character)
    {
        if (peek() != character)
        {
            return false;
        }

        get();

        return true;
    }

    int32_t peek() const
    {
        int32_t result = 0;

        if (mCurrent < mEnd)
        {
            #ifndef NDEBUG
            ptrdiff_t nConsumed =
            #endif
                traits_type::get(result, mCurrent, mEnd);
            assert(nConsumed > 0);
        }

        return result;
    }

private:
    const CharT* mCurrent;
    const CharT* mEnd;
}; // UnicodeCodepointIterator<CharT>

template<typename CharT>
UnicodeCodepointIterator<CharT> unicodeCodepointIterator(const std::basic_string<CharT>& s)
{
    return UnicodeCodepointIterator<CharT>(s);
}

template<typename CharT>
UnicodeCodepointIterator<CharT> unicodeCodepointIterator(const CharT* s, size_t length)
{
    return UnicodeCodepointIterator<CharT>(s, length);
}

template<typename CharT>
UnicodeCodepointIterator<CharT> unicodeCodepointIterator(const CharT* s)
{
    return UnicodeCodepointIterator<CharT>(s);
}

inline int hexval(const int c)
{
    return ((c & 0xf) + (c >> 6)) | ((c >> 3) & 0x8);
}

bool islchex_high(const int c);
bool islchex_low(const int c);

// gets a safe url by replacing private parts to be used in logs
std::string getSafeUrl(const std::string &posturl);

bool readLines(FileAccess& ifAccess, string_vector& destination);
bool readLines(InputStreamAccess& isAccess, string_vector& destination);
bool readLines(const std::string& input, string_vector& destination);

bool wildcardMatch(const string& text, const string& pattern);
bool wildcardMatch(const char* text, const char* pattern);

struct MEGA_API FileSystemAccess;

// generate a new drive id
handle generateDriveId(PrnGen& rng);

// return API_OK if success and set driveID handle to the drive id read from the drive,
// otherwise return error code and set driveId to UNDEF
error readDriveId(FileSystemAccess& fsAccess, const char* pathToDrive, handle& driveId);
error readDriveId(FileSystemAccess& fsAccess, const LocalPath& pathToDrive, handle& driveId);

// return API_OK if success, otherwise error code
error writeDriveId(FileSystemAccess& fsAccess, const char* pathToDrive, handle driveId);

int platformGetRLimitNumFile();

bool platformSetRLimitNumFile(int newNumFileLimit = -1);

void debugLogHeapUsage();

bool haveDuplicatedValues(const string_map& readableVals, const string_map& b64Vals);

struct SyncTransferCount
{
    bool operator==(const SyncTransferCount& rhs) const;
    bool operator!=(const SyncTransferCount& rhs) const;
    void operator-=(const SyncTransferCount& rhs);

    uint32_t mCompleted = 0;
    uint32_t mPending = 0;
    uint64_t mCompletedBytes = 0;
    uint64_t mPendingBytes = 0;
};

struct SyncTransferCounts
{
    bool operator==(const SyncTransferCounts& rhs) const;
    bool operator!=(const SyncTransferCounts& rhs) const;
    void operator-=(const SyncTransferCounts& rhs);

    // returns progress 0.0 to 1.0
    double progress(m_off_t inflightProgress) const;

    SyncTransferCount mDownloads;
    SyncTransferCount mUploads;
};

// creates a new id filling `id` with random bytes, up to `length`
void resetId(char* id, size_t length, PrnGen& rng);

// write messsage and strerror(aerrno) to log as an error
void reportError(const std::string& message, int aerrno = -1);

#ifdef WIN32

// as per (non C library standard) unix API
inline void sleep(int sec) {
    Sleep(sec * 1000);
}

// as per (non C library standard) unix API
// sleep for given number of microseconds
inline void usleep(int microsec) {
    Sleep(microsec / 1000);
}

// print messgae: error-num: error-description
void reportWindowsError(const std::string& message, DWORD error = 0xFFFFFFFF);

#endif // WIN32

// returns the direction type of a connection
string connDirectionToStr(direction_t directionType);

// Translate retry reason into a human-friendly string.
const char* toString(retryreason_t reason);

enum class CharType : uint8_t
{
    CSYMBOL = 0,
    CDIGIT = 1,
    CALPHA = 2,
};

// Wrapper functions for std::isspace and std::isdigit
// Not considering EOF values

/**
 * @brief Checks if a character is a whitespace character.
 *
 * @param ch The character to check
 * @return true if the character is a space, otherwise returns false.
 */
bool is_space(unsigned int ch);

/**
 * @brief Checks if a character is a digit.
 *
 * @param ch The character to check
 * @return true if the character is a digit (0-9), otherwise returns false.
 */
bool is_digit(unsigned int ch);

/**
 * @brief Checks if a character is a symbol.
 *
 * Note: this function is only valid for monobyte characters.
 *
 * @param ch The character to check
 * @return true if the character is a symbol, otherwise returns false
 */
bool is_symbol(unsigned int ch);

/**
 * @brief Determines the type of a given character.
 *
 * Valid values returned by this function are:
 * - CharType::CSYMBOL if the character is a symbol
 * - CharType::CDIGIT if the character is a digit
 * - CharType::CALPHA if the character is alphabetic
 *
 * @param ch The character to be classified
 * @return CharType representing the type of the character
 */
CharType getCharType(const unsigned int ch);

template<typename Container = std::set<std::string>>
Container splitString(const string& str, char delimiter)
{
    Container tokens;
    std::string token;
    std::istringstream tokenStream(str);
    while (std::getline(tokenStream, token, delimiter))
    {
        tokens.insert(tokens.end(), token);
    }

    return tokens;
}

template<typename Iter>
std::string joinStrings(
    const Iter begin,
    const Iter end,
    const std::string& separator,
    const std::function<std::string(const std::string&)> transform =
        [](const std::string& a) -> std::string
    {
        return a;
    })
{
    Iter position = begin;
    std::string result;
    if (position != end)
    {
        result += transform(*position++);
    }

    while (position != end)
    {
        result += separator + transform(*position++);
    }
    return result;
}

static constexpr char WILDCARD_MATCH_ONE = '?';
static constexpr char WILDCARD_MATCH_ALL = '*';
static constexpr char ESCAPE_CHARACTER = '\\';

std::string escapeWildCards(const std::string& pattern);

/**
 * @class TextPattern
 * @brief Helper class to store a text that will be used in a regex like search
 *
 * It stores the original text and an associated pattern to be used directly in the search adding
 * wild cards in both sides of the original text if needed. Example:
 * - text: hello -> pattern: *hello*
 * - text: * -> pattern: *
 *
 */
class TextPattern
{
public:
    TextPattern(const std::string& text);
    TextPattern(const char* text);

    TextPattern() = default;
    ~TextPattern() = default;
    TextPattern(const TextPattern& other) = default;
    TextPattern& operator=(const TextPattern& other) = default;
    TextPattern(TextPattern&& other) noexcept = default;
    TextPattern& operator=(TextPattern&& other) noexcept = default;

    const std::string& getText() const
    {
        return mText;
    }

    const std::string& getPattern() const
    {
        return mPattern;
    }

private:
    std::string mText;
    std::string mPattern;

    void recalcPattern();
    static bool isOnlyWildCards(const std::string& text);
};

std::set<std::string>::iterator getTagPosition(std::set<std::string>& tokens,
                                               const std::string& pattern,
                                               const bool stripAccents = true);

/*
 * Compare two UTF-8 strings for equality where the first string is
 * a "LIKE" expression. It is case and aceent insensitive.
 *
 * @param pattern the like pattern
 * @param str the UFT-8 string to compare against
 * @param esc the escape character
 * @param stripAccents True if accents should be stripped before comparison.
 *
 * @return true if the are the same and false if they are different
 */
bool likeCompare(const char* pattern,
                 const char* str,
                 const UChar32 esc = static_cast<UChar32>(ESCAPE_CHARACTER),
                 const bool stripAccents = true);

// Get the current process ID
unsigned long getCurrentPid();

// Convenience.
template<typename T>
struct IsStringType : std::false_type { };

template<>
struct IsStringType<std::string> : std::true_type { };

template<>
struct IsStringType<std::wstring> : std::true_type { };

// Retrieve a file's extension.
template<typename StringType>
auto extensionOf(const StringType& path, std::string& extension)
  -> typename std::enable_if<IsStringType<StringType>::value, bool>::type;

template<typename StringType>
auto extensionOf(const StringType& path)
  -> typename std::enable_if<IsStringType<StringType>::value, std::string>::type;

// Translate a character representing a hexadecimal digit to an integer.
template<typename T>
auto fromHex(char character)
  -> typename std::enable_if<std::is_integral<T>::value,
                             std::pair<T, bool>
                            >::type
{
    // Ensure the character's in lowercase.
    character |= ' ';

    // Character's a decimal digit.
    if (character >= '0' && character <= '9')
        return std::make_pair(static_cast<T>(character - '0'), true);

    // Character's a hexadecimal digit.
    if (character >= 'a' && character <= 'f')
        return std::make_pair(static_cast<T>(character - 'W'), true);

    // Character's not a valid hexadecimal digit.
    return std::make_pair(0, false);
}

// Translate a string of hexadecimal digits to an integer.
//
// NOTE: The string should be trimmed of any whitespace.
template<typename T>
auto fromHex(const char* current, const char* end)
  -> typename std::enable_if<std::is_integral<T>::value,
                             std::pair<T, bool>
                            >::type
{
    // What's the largest value that T can represent?
    constexpr auto maximum = std::numeric_limits<T>::max();

    // Convenience.
    constexpr auto undefined = std::make_pair(T{}, false);

    // An empty string doesn't contain a valid hex number.
    if (current == end)
        return undefined;

    // Our accumulated value.
    T value{};

    for ( ; current != end; ++current)
    {
        // Try and convert the current character to an integer.
        auto result = fromHex<T>(*current);

        // Character wasn't a valid hexadecimal digit.
        if (!result.second)
            return undefined;

        // Make sure we don't wrap.
        if (value && maximum / value < 16)
            return undefined;

        // Scale the value by 16.
        value *= 16;

        // Again, make sure we don't wrap.
        if (maximum - value < result.first)
            return undefined;

        // Include the new digit in our running total.
        value += result.first;
    }

    // Return value to caller.
    return std::make_pair(value, true);
}

template<typename T>
auto fromHex(const char* begin, std::size_t size)
  -> typename std::enable_if<std::is_integral<T>::value,
                             std::pair<T, bool>
                            >::type
{
    return fromHex<T>(begin, begin + size);
}

// Translate a string of hexadecimal digits to an integer.
//
// NOTE: The string should be trimmed of any whitespace.
template<typename T>
auto fromHex(const std::string& value)
  -> typename std::enable_if<std::is_integral<T>::value,
                             std::pair<T, bool>
                            >::type
{
    return fromHex<T>(value.data(), value.size());
}

// Convenience.
using SplitFragment =
  std::pair<const char*, std::size_t>;

using SplitResult =
  std::pair<SplitFragment, SplitFragment>;

// Split a string into two halves around a specific delimiter.
//
// NOTE: The second half includes the delimiter, if present.
SplitResult split(const char* begin, const char* end, char delimiter);
SplitResult split(const char* begin, const std::size_t size, char delimiter);
SplitResult split(const std::string& value, char delimiter);

/**
 * @brief Sorts input char strings using natural sorting ignoring case
 *
 * This function is only valid for comparing monobyte characters.
 * The default natural ascending order implemented by this function is:
 * Symbols < Numbers < Alphabetic_characters(# < 1 < a).
 *
 * Valid values returned by this function are:
 *  - if i == j returns 0
 *  - if i goes first returns a number greater than 0 (>=1)
 *  - if j goes first returns a number smaller than 0 (<=1)
 *
 * @param i Pointer to the first null-terminated string.
 * @param j Pointer to the second null-terminated string.
 *
 * @returns the order between 2 characters
 */
int naturalsorting_compare(const char* i, const char* j);

/**
 * @class NaturalSortingComparator
 * @brief A helper struct to be used in container templates such as std::set to force natural
 * sorting
 */
struct NaturalSortingComparator
{
    bool operator()(const std::string& lhs, const std::string& rhs) const
    {
        return naturalsorting_compare(lhs.c_str(), rhs.c_str()) < 0;
    }
};

/**
 * @class MrProper
 *
 * @brief Ensures execution of a cleanup function when the object goes out of scope.
 *
 * It accepts a std::function<void()> during construction, which it executes once upon destruction.
 * This is useful for resource management and ensuring cleanup in cases of exceptions.
 *
 * Example usage:
 *     void function() {
 *         MrProper cleaner([](){ std::cout << "Cleanup action executed.\n"; });
 *         // Any code here that might throw or return early
 *     }
 *
 * The class is non-copyable and non-movable to ensure the cleanup action is tightly bound to the
 * scope where it is declared.
 */
struct MrProper
{
    using CleanupFunction = std::function<void()>;
    CleanupFunction mOnRelease;

    ~MrProper()
    {
        mOnRelease();
    }

    explicit MrProper(std::function<void()> f):
        mOnRelease(f)
    {}

    MrProper() = delete;
    MrProper(const MrProper&) = delete;
    MrProper(MrProper&&) = delete;
    MrProper& operator=(const MrProper&) = delete;
    MrProper& operator=(MrProper&&) = delete;
};

/**
 * @brief Ensures the given string has an asterisk in front and back. If the string is empty, "*" is
 * returned.
 *
 * @note The input argument is passed by copy intentionally to operate on it.
 */
std::string ensureAsteriskSurround(std::string str);

/**
 * @brief Returns the index where the last '.' can be found in the fileName
 *
 * If there is not '.' in the input string, fileName.size() is returned
 *
 * @note This index is intended to be used with std::string::substr like:
 * size_t dotPos = fileExtensionDotPosition(fileName);
 * std::stirng basename = fileName.substr(0, dotPos);
 * std::stirng extension = fileName.substr(dotPos); // It will contain the '.' if present
 */
size_t fileExtensionDotPosition(const std::string& fileName);

// Helper function to combine hashes
inline uint64_t hashCombine(uint64_t seed, uint64_t value)
{
    // the magic number is the twos complement version of the golden ratio
    return seed ^ (value + 0x9e3779b97f4a7c15 + (seed << 12) + (seed >> 4));
}

/**
 * @class Timer
 * @brief A very simple helper struct to measure performance of some code pieces while developing a
 * time consuming part or refactoring.
 *
 * Example usage:
 * void exampleFunction() {
 *     {
 *         Timer t("Elapsed time: ", " ms");
 *         // Code block whose execution time you want to measure
 *         std::this_thread::sleep_for(std::chrono::milliseconds(100)); // Simulate work
 *     } // Timer destructs here, and the elapsed time is printed.
 * }
 */
template<typename DurUnit = std::chrono::milliseconds>
struct Timer
{
    Timer(const std::string& prefixMsg = "", const std::string& posfixMsg = ""):
        mPreMsg{prefixMsg},
        mPosMsg{posfixMsg},
        mStartTime{std::chrono::steady_clock::now()} // Correct initialization
    {}

    ~Timer()
    {
        const auto end = std::chrono::steady_clock::now();
        const auto dur = end - mStartTime; // Duration between start and end
        std::cout << mPreMsg << std::chrono::duration_cast<DurUnit>(dur).count() << mPosMsg << "\n";
    }

private:
    std::string mPreMsg;
    std::string mPosMsg;
    std::chrono::time_point<std::chrono::steady_clock> mStartTime; // Fixed time_point type
};

/**
 * @brief Returns std::this_thread:get_id() converted to a string
 */
std::string getThisThreadIdStr();

/**
 * @brief Converts a number of any arithmetic type to its string representation.
 *
 * @tparam T The type of the number to be converted. It must be an arithmetic type (e.g., int,
 * float, double).
 *
 * @param number The number to be converted to a string.
 * @return A `std::string` representing the number. If conversion fails or the type is not
 * arithmetic, an empty string is returned.
 *
 * @note This function only supports arithmetic types. The function will return an empty string if
 * the number cannot be successfully converted.
 */
template<typename T>
std::string numberToString(T number)
{
    static_assert(std::is_arithmetic_v<T>, "invalid numeric type");

    char buffer[64];
    if (auto [ptr, ec] = std::to_chars(buffer, buffer + sizeof(buffer), number); ec == std::errc())
    {
        return std::string(buffer, ptr);
    }

    return {};
}

} // namespace mega

#endif // MEGA_UTILS_H
