#include "mega/base64.h"
#include "mega/types.h"

#include <mega/common/date_time.h>
#include <mega/common/error_or.h>
#include <mega/common/node_info.h>
#include <mega/common/testing/path.h>
#include <mega/common/testing/utility.h>

#include <chrono>
#include <fstream>
#include <sstream>

namespace mega
{
namespace common
{
namespace testing
{

class StandardInputStream: public InputStreamAccess
{
    // What stream are we reading from?
    std::istream& mStream;

    // How much data does this stream contain?
    const m_off_t mSize;

public:
    StandardInputStream(std::istream& stream, m_off_t size);

    // Read count bytes from this stream into buffer.
    bool read(byte* buffer, unsigned int count) override;

    // How many bytes are available for reading?
    m_off_t size() override;
}; // StreamAccess

// Convenience.
using namespace fs;
using namespace std::chrono;

ErrorOr<FileFingerprint> fingerprint(const std::string& content, system_clock::time_point modified)
{
    // Convenience.
    auto modified_ = system_clock::to_time_t(modified);
    auto size_ = static_cast<m_off_t>(content.size());

    std::istringstream isstream(content, std::ios::binary);

    // Wrap input stream.
    StandardInputStream istream(isstream, size_);

    // Try and generate a fingerprint.
    FileFingerprint fingerprint;

    fingerprint.genfingerprint(&istream, modified_);

    // Couldn't generate fingerprint.
    if (!fingerprint.isvalid)
        return unexpected(API_EREAD);

    // Return fingerprint to caller.
    return fingerprint;
}

ErrorOr<FileFingerprint> fingerprint(const Path& path)
{
    std::error_code error;

    // Try and retrieve the file's modification time.
    auto modified = lastWriteTime(path, error);

    // Couldn't determine when the file was modified.
    if (error)
        return unexpected(API_EREAD);

    // Try and determine the file's size.
    auto size = file_size(path.path(), error);

    // Can't get the file's size.
    if (error)
        return unexpected(API_EREAD);

    // Open the file for reading.
    std::ifstream ifstream(path.string(), std::ios::binary);

    // Couldn't open the file for reading.
    if (!ifstream.is_open())
        return unexpected(API_EREAD);

    // Convenience.
    auto size_ = static_cast<m_off_t>(size);

    // Wrap the input stream.
    StandardInputStream istream(ifstream, size_);

    FileFingerprint fingerprint;

    // Try and generate a fingerprint.
    fingerprint.genfingerprint(&istream, modified);

    // Couldn't generate the fingerprint.
    if (!fingerprint.isvalid)
        return unexpected(API_EREAD);

    // Return fingerprint to caller.
    return fingerprint;
}

DateTime lastWriteTime(const Path& path)
{
    auto error = std::error_code();
    auto result = lastWriteTime(path, error);

    if (!error)
        return result;

    throw fs::filesystem_error("Couldn't retrieve modification time", path.path(), error);
}

void lastWriteTime(const Path& path, const DateTime& modified)
{
    std::error_code error;

    lastWriteTime(path, modified, error);

    if (!error)
        return;

    throw fs::filesystem_error("Couldn't set modification time", path.path(), error);
}

std::string randomBytes(std::size_t length)
{
    static std::mutex mutex;
    static PrnGen rng;

    std::lock_guard<std::mutex> guard(mutex);

    return rng.genstring(length);
}

std::string randomName()
{
    return Base64::btoa(randomBytes(16));
}

StandardInputStream::StandardInputStream(std::istream& stream, m_off_t size):
    InputStreamAccess(),
    mStream(stream),
    mSize(size)
{}

bool StandardInputStream::read(byte* buffer, unsigned int count)
{
    assert(buffer);
    assert(count);

    auto count_ = static_cast<std::streamsize>(count);

    mStream.read(reinterpret_cast<char*>(buffer), count_);

    return mStream.gcount() == count_;
}

m_off_t StandardInputStream::size()
{
    return mSize;
}

} // testing
} // fuse
} // mega
