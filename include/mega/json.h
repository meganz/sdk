/**
 * @file mega/json.h
 * @brief Linear non-strict JSON scanner
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

#ifndef MEGA_JSON_H
#define MEGA_JSON_H 1

#include "name_id.h"
#include "types.h"

namespace mega {

// linear non-strict JSON scanner
struct MEGA_API JSON
{
    JSON()
      : pos(nullptr)
    {
    }

    explicit JSON(const string& data)
      : pos(data.c_str())
    {
    }

    explicit JSON(const char* data)
      : pos(data)
    {
    }

    const char* pos;

    bool isnumeric();

    void begin(const char*);

    m_off_t getint();
    double getfloat();
    const char* getvalue();

    std::uint64_t getfsfp();
    uint64_t getuint64();

    nameid getnameid();
    nameid getnameid(const char*) const;

private:
    nameid getNameidSkipNull(bool skipnullvalues);
public:
    nameid getnameidvalue();

    string getname();
    string getnameWithoutAdvance() const;

    bool is(const char*);

    int storebinary(byte*, int);
    bool storebinary(string*);

    // MegaClient::NODEHANDLE
    bool ishandle(int = 6);
    handle gethandle(int = 6);
    NodeHandle getNodeHandle();

    bool enterarray();
    bool leavearray();

    bool enterobject();
    bool leaveobject();

    bool storeKeyValueFromObject(string& key, string& value);

    bool storeobject(string* = NULL);
    bool skipnullvalue();

    static void unescape(string*);

    /**
     * @brief Extract a string value for a name in a JSON string
     * @param json JSON string to check
     * @param name Attribute name.
     * @param value Atribute value.
     * @return False if the JSON string doesn't contains the string attribute
     */
    static bool extractstringvalue(const string & json, const string & name, string* value);

    // convenience functions, which avoid warnings and casts
    inline int      getint32()  { return int(getint()); }
    inline unsigned getuint32() { return unsigned(getint()); }
    inline bool     getbool()   { return bool(getint()); }

    // Only advance the pointer if it's an error (0, -1, -2, -3, ...)
    bool isNumericError(error& e);

    // copy JSON-delimited string
    static void copystring(string*, const char*);

    // Strip whitspace from a string in a JSON-safe manner.
    static string stripWhitespace(const string& text);
    static string stripWhitespace(const char* text);
};

class MEGA_API JSONWriter
{
public:
    JSONWriter();

    MEGA_DEFAULT_COPY_MOVE(JSONWriter)

    void cmd(const char*);
    void notself(MegaClient*);

    void arg(const char*, const string&, int = 1);
    void arg(const char*, const char*, int = 1);
    void arg(const char*, handle, int);
    void arg(const char*, NodeHandle);
    void arg(const char*, const byte*, int);
    void arg(const char*, m_off_t);
    void arg_B64(const char*, const string&);
    void arg_fsfp(const char*, std::uint64_t);

    // These should only be used when producing JSON meant for human consumption.
    // If you're generating JSON meant to be consumed by our servers, you
    // should escape things using arg_B64 above.
    void arg_stringWithEscapes(const char*, const char*, int = 1);
    void arg_stringWithEscapes(const char*, const string&, int = 1);

    void addcomma();
    void appendraw(const char*);
    void appendraw(const char*, int);
    void beginarray();
    void beginarray(const char*);
    void endarray();
    void beginobject();
    void beginobject(const char*);
    void endobject();
    void element(int);
    void element(handle, int = sizeof(handle));
    void element(const byte*, int);
    void element(const char* data);
    void element(const string& data);
    void element_B64(const string&);

    void openobject();
    void closeobject();

    const byte* getbytes() const;
    const string& getstring() const;

    size_t size() const;
    void clear() { mJson.clear(); }

protected:
    string escape(const char* data, size_t length) const;

private:
    static const int MAXDEPTH = 8;

    int elements();

    string mJson;
    std::array<signed char, MAXDEPTH> mLevels;
    signed char mLevel;
}; // JSONWriter


// Class to process JSON in streaming
// For performance reasons, these objects don't own the memory of the JSON buffer being parsed
// nor the map of filters used to trigger callbacks for the different JSON elements, so the caller
// must ensure that the memory is alive during the processing of JSON chunks
class MEGA_API JSONSplitter
{
public:
    JSONSplitter();

    // Reinitializes the object to start parsing a new JSON stream
    void clear();

    // Process a new chunk of JSON data and triggers callbacks in the filters map.
    // Returns the number of consumed bytes.
    //
    // The "filters" map allows to process the different JSON elements when they are complete
    //
    // The keys can be composed of these elements:
    // { or [ -> unnamed object or array
    // {name or [name -> object or array with the name "name"
    // "name -> string value for an attribute with name "name"
    // These alements can be appended to specify full paths, for example:
    // {[f{ -> unnamed objects, inside an array with the name "f", inside an unnamed object
    // {[ipc -> array with the name "ipc" inside an unnamed object
    //
    // The JSON object passed to the callback will contain the whole requested element,
    // except if anything was filtered inside. In that case, only the remaining data
    // would be passed to the callback.
    //
    // There are also special keys for specific purposes:
    // (empty string) -> Called when the parsing starts. An empty string is passed to the callback.
    // E -> A parsing error was detected. The callback will receive the current data in the stream
    // # -> An error was received, either a number or an error object {"err":XXX}
    // { -> The end of a JSON object. This is a normal case, but with the exception that
    //      if an error object is received, this callback won't be called.
    //
    // Callbacks in the map should return true on success and false if there was a parsing error, If
    // false is returned, the "E" callback will be triggered and the parsing will be aborted.
    //
    // "data" is the next chunk of JSON data to process. Initially it must be the beginning of the
    // JSON stream. The next chunk must start from the first non-consumed byte in the previous
    // call, which is at "data" + consumed_bytes (the return value of the previous call).
    // It is allowed to pass a different buffer for the next call, but it must
    // start with the same data that was not consumed during the previous call.
    m_off_t processChunk(std::map<std::string, std::function<bool(JSON *)>> *filters, const char* data);

    // Check if the parsing has finished
    bool hasFinished();

    // Check if the parsing has failed
    bool hasFailed();

    // Check if the parsing is starting
    bool isStarting();

protected:
    // Returns the position (in bytes) to the end of the current JSON string, or -1 if it's not found
    int strEnd();

    // Returns the position (in bytes) to the end of the current number, or -1 if it's not found
    int numEnd();

    // Called when there is a parsing error
    void parseError(std::map<std::string, std::function<bool(JSON *)>> *filters);

    // Check if there are any pending filter markers indicating that processing failed
    bool chunkProcessingFinishedSuccessfully(std::map<std::string, std::function<bool(JSON*)>>* filters);

    // Position of the character being processed (not owned by this object)
    const char* mPos = nullptr;

    // Position after the last filtered JSON path (not owned by this object)
    const char *mLastPos = nullptr;

    // Name of the last JSON attribute name processed
    std::string mLastName;

    // Stack with accessed paths in the JSON stream
    std::vector<std::string> mStack;

    // Current path in the processing of the JSON stream
    std::string mCurrentPath;

    // Bytes processed since the last discarded byte.
    // Despite those bytes were already processed, they are not discarded yet
    // because they belong to a JSON element that hasn't been totally
    // received nor filtered yet.
    m_off_t mProcessedBytes = 0;

    // 0: no value expected, 1: optional value expected, -1: compulsory value expected
    int mExpectValue = 1;

    // the parsing is starting
    bool mStarting = true;

    // the parsing has finished
    bool mFinished = false;

    // the parsing has failed
    bool mFailed = false;

}; // JSONSplitter

// If true, logs the contents of all JSON requests and responses in full.
extern std::atomic<bool> gLogJSONRequests;

} // namespace

#endif
