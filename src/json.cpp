/**
 * @file json.cpp
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
#include <cctype>
#include <cstdint>

#include "mega/json.h"
#include "mega/base64.h"
#include "mega/megaclient.h"
#include "mega/logging.h"
#include "mega/mega_utf8proc.h"

namespace mega {

std::atomic<bool> gLogJSONRequests{false};

#define JSON_verbose if (gLogJSONRequests) LOG_verbose

// store array or object in string s
// reposition after object
bool JSON::storeobject(string* s)
{
    int openobject[2] = { 0 };
    const char* ptr;
    bool escaped = false;

    while (*(const signed char*)pos > 0 && *pos <= ' ')
    {
        pos++;
    }

    if (*pos == ']' || *pos == '}')
    {
        return false;
    }

    if (*pos == ',')
    {
        pos++;
    }

    ptr = pos;

    for (;;)
    {
        if ((*ptr == '[') || (*ptr == '{'))
        {
            openobject[*ptr == '[']++;
        }
        else if ((*ptr == ']') || (*ptr == '}'))
        {
            openobject[*ptr == ']']--;
            if(openobject[*ptr == ']'] < 0)
            {
                LOG_err << "Parse error (])";
            }
        }
        else if (*ptr == '"')
        {
            ptr++;

            while (*ptr && (escaped || *ptr != '"'))
            {
                escaped = *ptr == '\\' && !escaped;
                ptr++;
            }

            if (!*ptr)
            {
                LOG_err << "Parse error (\")";
                return false;
            }
        }
        else if ((*ptr >= '0' && *ptr <= '9') || *ptr == '-' || *ptr == '.')
        {
            ptr++;

            while ((*ptr >= '0' && *ptr <= '9') || *ptr == '.' || *ptr == 'e' || *ptr == 'E')
            {
                ptr++;
            }

            ptr--;
        }
        else if (*ptr != ':' && *ptr != ',')
        {
            LOG_err << "Parse error (unexpected " << *ptr << ")";
            return false;
        }

        ptr++;

        if (!openobject[0] && !openobject[1])
        {
            if (s)
            {
                if (*pos == '"')
                {
                    s->assign(pos + 1, static_cast<size_t>(ptr - pos - 2));
                }
                else
                {
                    s->assign(pos, static_cast<size_t>(ptr - pos));
                }
            }

            pos = ptr;
            return true;
        }
    }
}

bool JSON::storeKeyValueFromObject(string& key, string& value)
{
    // this one can be used when the key is not a nameid
    if (!storeobject(&key) || *pos != ':')
    {
        return false;
    }

    ++pos;

    return storeobject(&value);
}

bool JSON::skipnullvalue()
{
    // this applies only to values, after ':'
    if (!pos)
        return false;

    switch (*pos)
    {
    case ',':         // empty value, i.e.  "foo":,
        ++pos;
    // fall through
    case ']':         // empty value, i.e.  "foo":]
    case'}':          // empty value, i.e.  "foo":}
        return true;

    default:          // some other value, don't skip it
        return false;

    case 'n':
        if (strncmp(pos, "null", 4))
            return false; // not enough information to skip it

        assert(false); // the MEGA servers should never send null.  Investigation needed.

        // let's peak at what's after "null"
        switch (*(pos + 4))
        {
        case ',':     // null value, i.e.  "foo":null,
            ++pos;
        // fall through
        case ']':     // null value, i.e.  "foo":null]
        case '}':     // null value, i.e.  "foo":null}
            pos += 4;
            return true;

        default:      // some other value, don't skip it
            return false;
        }
    }
}

bool JSON::isnumeric()
{
    if (*pos == ',')
    {
        pos++;
    }

    const char* ptr = pos;

    if (*ptr == '-')
    {
        ptr++;
    }

    return *ptr >= '0' && *ptr <= '9';
}

nameid JSON::getnameid(const char* ptr) const
{
    nameid id = EOO;

    while (*ptr && *ptr != '"')
    {
        id = (id << 8) + static_cast<nameid>(*ptr++);
    }

    return id;
}

nameid JSON::getnameid()
{
    return getNameidSkipNull(true);
}

nameid JSON::getnameidvalue()
{
    return getNameidSkipNull(false);
}

std::string JSON::getname()
{
    const char* ptr = pos;
    string name;

    if (*ptr == ',' || *ptr == ':')
    {
        ptr++;
    }

    if (*ptr++ == '"')
    {
        while (*ptr && *ptr != '"')
        {
            name += *ptr;
            ptr++;
        }

        pos = ptr + 2;
    }

    return name;
}

std::string JSON::getnameWithoutAdvance() const
{
    const char* ptr = pos;
    string name;

    if (*ptr == ',' || *ptr == ':')
    {
        ptr++;
    }

    if (*ptr++ == '"')
    {
        while (*ptr && *ptr != '"')
        {
            name += *ptr;
            ptr++;
        }
    }

    return name;
}

// pos points to [,]"name":...
// returns nameid and repositons pos after :
// no unescaping supported
nameid JSON::getNameidSkipNull(bool skipnullvalues)
{
    const char* ptr = pos;
    nameid id = 0;

    if (*ptr == ',' || *ptr == ':')
    {
        ptr++;
    }

    if (*ptr++ == '"')
    {
        while (*ptr && *ptr != '"')
        {
            id = (id << 8) + static_cast<nameid>(*ptr++);
        }

        assert(*ptr == '"'); // if either assert fails, check the json syntax, it might be something new/changed
        pos = ptr + 1;

        if (*pos == ':' || *pos == ',' )
        {
            pos++;
        }
        else
        {
            // don't skip the char if we're at the end of a structure eg. actionpacket with only {"a":"xyz"}
            assert(*pos == '}' || *pos == ']');
        }
    }

    bool skippedNull = id && skipnullvalues && skipnullvalue();

    return skippedNull ? getnameid() : id;
}

// specific string comparison/skipping
bool JSON::is(const char* value)
{
    if (*pos == ',')
    {
        pos++;
    }

    if (*pos != '"')
    {
        return false;
    }

    size_t t = strlen(value);

    if (memcmp(pos + 1, value, t) || pos[t + 1] != '"')
    {
        return false;
    }

    pos += t + 2;

    return true;
}

// base64-decode binary value to designated fixed-length buffer
int JSON::storebinary(byte* dst, int dstlen)
{
    int l = 0;

    if (*pos == ',')
    {
        pos++;
    }

    if (*pos == '"')
    {
        l = Base64::atob(pos + 1, dst, dstlen);

        // skip string
        storeobject();
    }

    return l;
}

// base64-decode binary value to designated string
bool JSON::storebinary(string* dst)
{
    if (*pos == ',')
    {
        pos++;
    }

    if (*pos == '"')
    {
        const char* ptr;

        ptr = strchr(pos + 1, '"');
        if (!ptr)
        {
            LOG_err << "Parse error (storebinary)";
            return false;
        }

        dst->resize(static_cast<size_t>((ptr - pos - 1) / 4 * 3 + 3));
        dst->resize(
            static_cast<size_t>(Base64::atob(pos + 1, (byte*)dst->data(), int(dst->size()))));

        // skip string
        storeobject();
    }

    return true;
}

// test for specific handle type
bool JSON::ishandle(int size)
{
    size = (size == 6) ? 8 : 11;

    if (*pos == ',')
    {
        pos++;
    }

    if (*pos == '"')
    {
        int i;

        // test for short string
        for (i = 0; i <= size; i++)
        {
            if (!pos[i])
            {
                return false;
            }
        }

        return pos[i] == '"';
    }

    return false;
}

// decode handle
handle JSON::gethandle(int size)
{
    byte buf[9] = { 0 };

    // no arithmetic or semantic comparisons will be performed on handles, so
    // no endianness issues
    if (storebinary(buf, sizeof buf) == size)
    {
        return MemAccess::get<handle>((const char*)buf);
    }

    return UNDEF;
}

NodeHandle JSON::getNodeHandle()
{
    return NodeHandle().set6byte(gethandle(6));
}

// decode integer
m_off_t JSON::getint()
{
    const char* ptr;

    if (*pos == ':' || *pos == ',')
    {
        pos++;
    }

    ptr = pos;

    if (*ptr == '"')
    {
        ptr++;
    }

    if ((*ptr < '0' || *ptr > '9') && *ptr != '-')
    {
        LOG_err << "Parse error (getint)";
        // An event about failing to parse the json cannot be sent because no MegaClient instance is
        // accessible from here.
        assert(false && "JSON::getint(): Unexpected value in JSON");
        // It should probably return something less common in this case, like
        // std::numeric_limits<m_off_t>::min().
        return -1;
    }

    handle r = static_cast<handle>(atoll(ptr));
    storeobject();

    return static_cast<m_off_t>(r);
}

// decode float
double JSON::getfloat()
{
    if (*pos == ':' || *pos == ',')
    {
        pos++;
    }

    if ((*pos < '0' || *pos > '9') && *pos != '-' && *pos != '.')
    {
        LOG_err << "Parse error (getfloat)";
        return -1;
    }

    double r = atof(pos);

    storeobject();

    return r;
}

// return pointer to JSON payload data
const char* JSON::getvalue()
{
    const char* r;

    if (*pos == ':' || *pos == ',')
    {
        pos++;
    }

    if (*pos == '"')
    {
        r = pos + 1;
    }
    else
    {
        r = pos;
    }

    storeobject();

    return r;
}

std::uint64_t JSON::getfsfp()
{
    return gethandle(sizeof(std::uint64_t));
}

uint64_t JSON::getuint64()
{
    const char* ptr;

    if (*pos == ':' || *pos == ',')
    {
        pos++;
    }

    ptr = pos;

    if (*ptr == '"')
    {
        ptr++;
    }

    if (!is_digit(static_cast<unsigned>(*ptr)))
    {
        LOG_err << "Parse error (getuint64)";
        return std::numeric_limits<uint64_t>::max();
    }

    uint64_t r = strtoull(ptr, nullptr, 0);
    storeobject();

    return r;
}

// try to to enter array
bool JSON::enterarray()
{
    if (*pos == ',' || *pos == ':')
    {
        pos++;
    }

    if (*pos == '[')
    {
        pos++;
        return true;
    }

    return false;
}

// leave array (must be at end of array)
bool JSON::leavearray()
{
    if (*pos == ']')
    {
        pos++;
        return true;
    }

    LOG_err << "Parse error (leavearray)";
    return false;
}

// try to enter object
bool JSON::enterobject()
{
    if (*pos == '}')
    {
        pos++;
    }
    if (*pos == ',')
    {
        pos++;
    }

    if (*pos == '{')
    {
        pos++;
        return true;
    }

    return false;
}

// leave object (skip remainder)
bool JSON::leaveobject()
{
    for (; ;)
    {
        if (*pos == ':' || *pos == ',' || *pos == ' ')
        {
            pos++;
        }
        else if (*pos == '"'
                || (*pos >= '0' && *pos <= '9')
                || *pos == '-'
                || *pos == '['
                || *pos == '{')
        {
            storeobject();
        }
        else if(*pos == ']')
        {
            LOG_err << "Parse error (unexpected ']' character)";
            pos++;
        }
        else
        {
            break;
        }
    }

    if (*pos == '}')
    {
        pos++;
        return true;
    }

    LOG_err << "Parse error (leaveobject)";
    return false;
}

// unescape JSON string (non-strict)
void JSON::unescape(string* s)
{
    char c;
    int l;

    for (unsigned i = 0; i + 1 < s->size(); i++)
    {
        if ((*s)[i] == '\\')
        {
            switch ((*s)[i + 1])
            {
                case 'n':
                    c = '\n';
                    l = 2;
                    break;

                case 'r':
                    c = '\r';
                    l = 2;
                    break;

                case 'b':
                    c = '\b';
                    l = 2;
                    break;

                case 'f':
                    c = '\f';
                    l = 2;
                    break;

                case 't':
                    c = '\t';
                    l = 2;
                    break;

                case '\\':
                    c = '\\';
                    l = 2;
                    break;

                case 'u':
                    c = static_cast<char>((hexval((*s)[i + 4]) << 4) | hexval((*s)[i + 5]));
                    l = 6;
                    break;

                default:
                    c = (*s)[i + 1];
                    l = 2;
            }

            s->replace(i, static_cast<size_t>(l), &c, 1);
        }
    }
}

bool JSON::extractstringvalue(const string &json, const string &name, string *value)
{
    string pattern = name + "\":\"";
    size_t pos = json.find(pattern);
    if (pos == string::npos)
    {
        return false;
    }

    size_t end = json.find("\"", pos + pattern.length());
    if (end == string::npos)
    {
        return false;
    }

    *value = json.substr(pos + pattern.size(), end - pos - pattern.size());
    return true;
}

bool JSON::isNumericError(error &e)
{
    const char* ptr = pos;
    if (*ptr == ',')
    {
        ptr++;
    }

    const char* auxPtr = ptr;
    if (*auxPtr != '-' && *auxPtr != '0')
    {
        e = API_OK;
        return false;
    }

    if (*auxPtr == '-')
    {
        auxPtr++;
        if (!(*auxPtr >= '1' && *auxPtr <= '9'))
        {
            e = API_OK;
            return false;
        }
    }

    e = static_cast<error>(atoll(ptr));
    storeobject();

    return true;
}

// position at start of object
void JSON::begin(const char* json)
{
    pos = json;
}

// copy remainder of quoted string (no unescaping, use for base64 data only)
void JSON::copystring(string* s, const char* p)
{
    if (p)
    {
        const char* pp;

        pp = strchr(p, '"');
        if (pp)
        {
            s->assign(p, static_cast<size_t>(pp - p));
        }
        else
        {
            *s = p;
        }
    }
    else
    {
        s->clear();
    }
}

string JSON::stripWhitespace(const string& text)
{
    return stripWhitespace(text.c_str());
}

string JSON::stripWhitespace(const char* text)
{
    JSON reader(text);
    string result;

    while (*reader.pos)
    {
        if (*reader.pos == '"')
        {
            string temp;

            result.push_back('"');

            if (!reader.storeobject(&temp))
                return result;

            result.append(temp);
            result.push_back('"');
        }
        else if (is_space(static_cast<unsigned>(*reader.pos)))
            ++reader.pos;
        else
            result.push_back(*reader.pos++);
    }

    return result;
}

JSONWriter::JSONWriter()
  : mJson()
  , mLevels()
  , mLevel(-1)
{
}

void JSONWriter::cmd(const char* cmd)
{
    mJson.append("\"a\":\"");
    mJson.append(cmd);
    mJson.append("\"");
}

void JSONWriter::notself(MegaClient* client)
{
    mJson.append(",\"i\":\"");
    mJson.append(client->sessionid, sizeof client->sessionid);
    mJson.append("\"");
}

void JSONWriter::arg(const char* name, const string& value, int quotes)
{
    arg(name, value.c_str(), quotes);
}

void JSONWriter::arg(const char* name, const char* value, int quotes)
{
    addcomma();
    mJson.append("\"");
    mJson.append(name);
    mJson.append(quotes ? "\":\"" : "\":");
    mJson.append(value);

    if (quotes)
    {
        mJson.append("\"");
    }
}

void JSONWriter::arg(const char* name, handle h, int len)
{
    char buf[16];

    Base64::btoa((const byte*)&h, len, buf);

    arg(name, buf);
}

void JSONWriter::arg(const char* name, NodeHandle h)
{
    arg(name, h.as8byte(), 6);
}


void JSONWriter::arg(const char* name, const byte* value, int len)
{
    char* buf = new char[static_cast<size_t>(len * 4 / 3 + 4)];

    Base64::btoa(value, len, buf);

    arg(name, buf);

    delete[] buf;
}

void JSONWriter::arg_B64(const char* n, const string& data)
{
    arg(n, (const byte*)data.data(), int(data.size()));
}

void JSONWriter::arg_fsfp(const char* n, std::uint64_t fp)
{
    arg(n, (const byte*)&fp, int(sizeof(fp)));
}

void JSONWriter::arg_stringWithEscapes(const char* name, const string& value, int quote)
{
    arg(name, escape(value.c_str(), value.size()), quote);
}

void JSONWriter::arg_stringWithEscapes(const char* name, const char* value, int quote)
{
    arg(name, escape(value, strlen(value)), quote);
}

void JSONWriter::arg(const char* name, m_off_t n)
{
    char buf[32];

    snprintf(buf, sizeof(buf), "%" PRId64, n);

    arg(name, buf, 0);
}

void JSONWriter::addcomma()
{
    if (mJson.size() && !strchr("[{", mJson[mJson.size() - 1]))
    {
        mJson.append(",");
    }
}

void JSONWriter::appendraw(const char* s)
{
    mJson.append(s);
}

void JSONWriter::appendraw(const char* s, int len)
{
    mJson.append(s, static_cast<size_t>(len));
}

void JSONWriter::beginarray()
{
    addcomma();
    mJson.append("[");
    openobject();
}

void JSONWriter::beginarray(const char* name)
{
    addcomma();
    mJson.append("\"");
    mJson.append(name);
    mJson.append("\":[");
    openobject();
}

void JSONWriter::endarray()
{
    mJson.append("]");
    closeobject();
}

void JSONWriter::beginobject()
{
    addcomma();
    mJson.append("{");
}

void JSONWriter::beginobject(const char* name)
{
    addcomma();
    mJson.append("\"");
    mJson.append(name);
    mJson.append("\":{");
}

void JSONWriter::endobject()
{
    mJson.append("}");
}

void JSONWriter::element(int n)
{
    if (elements())
    {
        mJson.append(",");
    }
    mJson.append(std::to_string(n));
}

void JSONWriter::element(handle h, int len)
{
    char buf[16];

    Base64::btoa((const byte*)&h, len, buf);

    mJson.append(elements() ? ",\"" : "\"");
    mJson.append(buf);
    mJson.append("\"");
}

void JSONWriter::element(const byte* data, int len)
{
    char* buf = new char[static_cast<size_t>(len * 4 / 3 + 4)];

    len = Base64::btoa(data, len, buf);

    mJson.append(elements() ? ",\"" : "\"");
    mJson.append(buf, static_cast<size_t>(len));

    delete[] buf;

    mJson.append("\"");
}

void JSONWriter::element(const char* data)
{
    mJson.append(elements() ? ",\"" : "\"");
    mJson.append(data);
    mJson.append("\"");
}

void JSONWriter::element(const string& data)
{
    element(data.c_str());
}

void JSONWriter::element_B64(const string& s)
{
    element((const byte*)s.data(), int(s.size()));
}

void JSONWriter::openobject()
{
    mLevels[static_cast<size_t>(++mLevel)] = 0;
}

void JSONWriter::closeobject()
{
    --mLevel;
}

const byte* JSONWriter::getbytes() const
{
    return reinterpret_cast<const byte*>(mJson.data());
}

const string& JSONWriter::getstring() const
{
    return mJson;
}

size_t JSONWriter::size() const
{
    return mJson.size();
}

int JSONWriter::elements()
{
    assert(mLevel >= 0);

    if (!mLevels[static_cast<size_t>(mLevel)])
    {
        mLevels[static_cast<size_t>(mLevel)] = 1;
        return 0;
    }

    return 1;
}

string JSONWriter::escape(const char* data, size_t length) const
{
    const utf8proc_uint8_t* current = reinterpret_cast<const utf8proc_uint8_t *>(data);
    utf8proc_ssize_t remaining = static_cast<utf8proc_ssize_t>(length);
    utf8proc_int32_t codepoint = 0;
    string result;

    while (remaining > 0)
    {
        auto read = utf8proc_iterate(current, remaining, &codepoint);
        assert(codepoint >= 0);
        assert(read > 0);

        current += read;
        remaining -= read;

        if (read > 1)
        {
            result.append(current - read, current);
            continue;
        }

        switch (codepoint)
        {
        case '"':
            result.append("\\\"");
            break;
        case '\\':
            result.append("\\\\");
            break;
        default:
            result.push_back(static_cast<char>(current[-1]));
            break;
        }
    }

    return result;
}

JSONSplitter::JSONSplitter()
{
    clear();
}

void JSONSplitter::clear()
{
    mPos = nullptr;
    mLastPos = nullptr;
    mLastName.clear();
    mStack.clear();
    mCurrentPath.clear();
    mProcessedBytes = 0;
    mExpectValue = 1;
    mStarting = true;
    mFinished = false;
    mFailed = false;
}

m_off_t JSONSplitter::processChunk(
    std::map<string, std::function<bool (JSON *)> > *filters,
    const char *data,
    std::map<string, std::function<bool (JSON *)> > *preFilters)
{
    if (hasFailed() || hasFinished())
    {
        return 0;
    }

    if (!mSuspended)
    {
        if (filters)
        {
            auto filterit = filters->find("<");
            if (filterit != filters->end())
            {
                JSON jsonData("");
                auto& callback = filterit->second;
                if (!callback(&jsonData))
                {
                    LOG_err << "Error starting the processing of a chunk";
                }
            }
        }

        mPos = data;
        mLastPos = data;

        // Skip the data that was already processed during the previous call
        mPos += mProcessedBytes;
        mProcessedBytes = 0;

        if (mStarting)
        {
            if (filters)
            {
                auto filterit = filters->find("");
                if (filterit != filters->end())
                {
                    JSON jsonData("");
                    auto& callback = filterit->second;
                    if (!callback(&jsonData))
                    {
                        LOG_err << "Parsing error processing first streaming filter"
                                << " Data: " << data;
                        parseError(filters);
                        return 0;
                    }
                }
            }
            mStarting = false;
        }
    }
    else
    {
        mSuspended = false;
    }

    JSON_verbose << "JSON starting processChunk at path " << mCurrentPath
                 << " ExpectValue: " << mExpectValue << " LastName: " << mLastName
                 << " Data: " << std::string(data, strlen(data) < 32 ? strlen(data) : 32)
                 << " Start: " << std::string(mPos, strlen(mPos) < 32 ? strlen(mPos) : 32);

    while (*mPos)
    {
        char c = *mPos;
        if (c == '[' || c == '{')
        {
            if (!mExpectValue)
            {
                LOG_err << "Malformed JSON - unexpected object or array";
                parseError(filters);
                return 0;
            }

            mStack.push_back(c + mLastName);
            mCurrentPath.append(mStack.back());

            JSON_verbose << "JSON starting path: " << mCurrentPath;
            if (filters && filters->find(mCurrentPath) != filters->end())
            {
                // a filter is configured for this path - recurse
                mLastPos = mPos;
            }

            mPos++;
            mLastName.clear();
            mExpectValue = c == '[';
        }
        else if (c == ']' || c == '}')
        {
            if (mExpectValue < 0)
            {
                LOG_err << "Malformed JSON - premature closure";
                parseError(filters);
                return 0;
            }

            char open = mStack[mStack.size() - 1][0];
            if (!mStack.size() || (c == ']' && open != '[') || (c == '}' && open != '{'))
            {
                LOG_err << "Malformed JSON - mismatched close";
                parseError(filters);
                return 0;
            }

            // Backup for suspend
            std::string lastName = mLastName;

            mLastName.clear();
            mPos++;

            // check if this concludes an exfiltrated object and return it if so
            if (filters)
            {
                std::string filter;
                if (mStack.size() == 1 && c == '}' && mLastPos == data
                    && strncmp(data, "{\"err\":", 7) == 0)
                {
                    // error response
                    filter = "#";
                }
                else
                {
                    // regular closure
                    filter = mCurrentPath;
                }

                auto filterit = filters->find(filter);
                if (filterit != filters->end() && filterit->second)
                {
                    // PreFilter is used to decide whether to run filter or suspend the process
                    if (preFilters)
                    {
                        auto preFilterit = preFilters->find(filter);
                        if (preFilterit != preFilters->end() && preFilterit->second)
                        {
                            JSON preJsonData(mLastPos);
                            auto& preCallback = preFilterit->second;
                            if (!preCallback(&preJsonData))
                            {
                                // Restore data for resume
                                mPos--;
                                mLastName = lastName;

                                mSuspended = true;

                                return -1;
                            }
                        }
                    }

                    JSON_verbose << "JSON object/array callback for path: " << filter << " Data: "
                                 << std::string(mLastPos, static_cast<size_t>(mPos - mLastPos));

                    JSON jsonData(mLastPos);
                    auto& callback = filterit->second;
                    if (!callback(&jsonData))
                    {
                        LOG_err << "Parsing error processing streaming filter: " << filter
                                << " Data: "
                                << std::string(mLastPos, static_cast<size_t>(mPos - mLastPos));
                        parseError(filters);
                        return 0;
                    }

                    // Callbacks should consume the exact amount of JSON, except the last one
                    if (mCurrentPath != "{" && jsonData.pos != mPos)
                    {
                        // I'm not aborting the parsing here because no errors were detected during the processing
                        // so probably the callback just ignored some data that it didn't need.
                        // Anyway it would be good to check this when it happens to fix it.
                        LOG_warn << (mPos - jsonData.pos) << " bytes were not processed by the following streaming filter: " << filter;
                        assert(false);
                    }

                    mLastPos = mPos;
                }
            }

            JSON_verbose << "JSON finishing path: " << mCurrentPath;
            mCurrentPath.resize(mCurrentPath.size() - mStack.back().size());
            mStack.pop_back();
            mExpectValue = 0;

            if (!mStack.size())
            {
                assert(mCurrentPath.empty());

                mLastPos = mPos;
                mFinished = true;
                break;
            }
        }
        else if (c == ',')
        {
            if (mExpectValue)
            {
                LOG_err << "Malformed JSON - stray comma";
                parseError(filters);
                return 0;
            }

            if (mLastPos == mPos)
            {
                mLastPos++;
            }

            mPos++;
            mExpectValue = mStack[mStack.size() - 1][0] == '[';
        }
        else if (c == '"')
        {
            int t = strEnd();
            if (t < 0)
            {
                JSON_verbose << "JSON chunk finished parsing a string."
                             << " Data: " << mPos;
                break;
            }

            if (mExpectValue)
            {
                if (filters)
                {
                    std::string filter = mCurrentPath + c + mLastName;
                    auto filterit = filters->find(filter);
                    if (filterit != filters->end() && filterit->second)
                    {
                        // PreFilter is used to decide whether to run filter or suspend the process
                        if (preFilters)
                        {
                            auto preFilterit = preFilters->find(filter);
                            if (preFilterit != preFilters->end() && preFilterit->second)
                            {
                                JSON preJsonData(mPos);
                                auto& preCallback = preFilterit->second;
                                if (!preCallback(&preJsonData))
                                {
                                    mSuspended = true;
                                    return -1;
                                }
                            }
                        }

                        JSON_verbose << "JSON string value callback for: " << filter
                                     << " Data: " << std::string(mPos, static_cast<size_t>(t));

                        JSON jsonData(mPos);
                        auto& callback = filterit->second;
                        if (!callback(&jsonData))
                        {
                            LOG_err << "Parsing error processing streaming filter: " << filter
                                    << " Data: " << std::string(mPos, static_cast<size_t>(t));
                            parseError(filters);
                            return 0;
                        }

                        mLastPos = mPos + t;
                    }
                }

                JSON_verbose << "JSON string value parsed at path " << mCurrentPath
                             << " Data: " << mLastName << " = "
                             << std::string(mPos + 1, static_cast<size_t>(t - 2));

                mPos += t;
                mExpectValue = 0;
                mLastName.clear();
            }
            else
            {
                // we need at least one char after end of property string
                if (!mPos[t])
                {
                    break;
                }

                if (mPos[t] != ':')
                {
                    LOG_err << "Malformed JSON - no : found after property name";
                    parseError(filters);
                    return 0;
                }

                JSON_verbose << "JSON property name parsed at path " << mCurrentPath
                             << " Data: " << std::string(mPos + 1, static_cast<size_t>(t - 2));

                mLastName = std::string(mPos + 1, static_cast<size_t>(t - 2));
                mPos += t + 1;
                mExpectValue = -1;
            }
        }
        else if ((c >= '0' && c <= '9') || c == '.' || c == '-')
        {
            if (!mExpectValue)
            {
                LOG_err << "Malformed JSON - unexpected number";
                parseError(filters);
                return 0;
            }

            int j = numEnd();
            if (j < 0 || !mPos[j])
            {
                JSON_verbose << "JSON chunk finished parsing a number."
                             << " Data: " << mPos;
                break;
            }

            JSON_verbose << "JSON number parsed at path " << mCurrentPath << " Data: " << mLastName
                         << " = " << std::string(mPos, static_cast<size_t>(j));

            mPos += j;
            mExpectValue = 0;

            if (!mStack.size())
            {
                assert(mCurrentPath.empty());

                if (filters && mLastPos == mPos - j)
                {
                    assert(mLastPos == data);

                    auto filterit = filters->find("#");
                    if (filterit != filters->end())
                    {
                        JSON jsonData(mLastPos);
                        JSON_verbose << "JSON error callback."
                                     << " Data: " << std::string(mLastPos, static_cast<size_t>(j));

                        auto& callback = filterit->second;
                        if (!callback(&jsonData))
                        {
                            LOG_err << "Parsing error processing error streaming filter"
                                    << " Data: " << std::string(mLastPos, static_cast<size_t>(j));
                            parseError(filters);
                            return 0;
                        }
                    }
                }

                mLastPos = mPos;
                mFinished = true;
                break;
            }
        }
        else if (c == ' ')
        {
            // a concession to the API team's aesthetic sense
            mPos++;
        }
        else
        {
            LOG_err << "Malformed JSON - bogus char at position " << (mPos - data);
            parseError(filters);
            return 0;
        }
    }

    if (filters && !chunkProcessingFinishedSuccessfully(filters))
    {
        LOG_err << "Error finishing the processing of a chunk";
    }

    mProcessedBytes = mPos - mLastPos;
    m_off_t consumedBytes = mLastPos - data;

    JSON_verbose << "JSON leaving processChunk at path " << mCurrentPath << "."
                 << " Data: " << std::string(mPos, strlen(mPos) < 32 ? strlen(mPos) : 32)
                 << " Processed: " << mProcessedBytes << " Consumed: " << consumedBytes
                 << " Next start: " << std::string(mLastPos, strlen(mLastPos) < 32 ? strlen(mLastPos) : 32);

    return consumedBytes;
}

bool JSONSplitter::hasFinished()
{
    return mFinished;
}

bool JSONSplitter::hasFailed()
{
    return mFailed;
}

bool JSONSplitter::isStarting()
{
    return mStarting;
}

int JSONSplitter::strEnd()
{
    const char* ptr = mPos;
    while ((ptr = strchr(ptr + 1, '"')) != nullptr)
    {
        const char *e = ptr;
        while (*(--e) == '\\')
        {
            // noop
        }

        if ((ptr - e) & 1)
        {
            return int(ptr + 1 - mPos);
        }
    }

    return -1;
}

int JSONSplitter::numEnd()
{
    const char* ptr = mPos;
    while (*ptr && strchr("0123456789-+eE.", *ptr))
    {
        ptr++;
    }

    if (ptr > mPos)
    {
        return int(ptr - mPos);
    }

    return -1;
}

void JSONSplitter::parseError(std::map<string, std::function<bool (JSON *)> > *filters)
{
    if (filters)
    {
        auto filterit = filters->find("E");
        if (filterit != filters->end() && filterit->second)
        {
            JSON jsonData(mPos);
            auto& callback = filterit->second;
            callback(&jsonData);
        }

        if (!chunkProcessingFinishedSuccessfully(filters))
        {
            LOG_err << "Error finishing the processing of a chunk after error";
        }
    }
    mFailed = true;
    assert(false);
}

bool JSONSplitter::chunkProcessingFinishedSuccessfully(std::map<std::string, std::function<bool(JSON*)>>* filters)
{
    auto filterit = filters->find(">");
    if (filterit != filters->end())
    {
        JSON jsonData("");
        auto& callback = filterit->second;
        if (!callback(&jsonData))
        {
            return false;
        }
    }
    return true;
}

} // namespace

