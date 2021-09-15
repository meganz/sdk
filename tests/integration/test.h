#pragma once
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "stdfs.h"

std::string logTime();

class LogStream
{
public:
    LogStream()
      : mBuffer()
    {
    }

    LogStream(LogStream&& other) noexcept
      : mBuffer(std::move(other.mBuffer))
    {
    }

    ~LogStream();

    template<typename T>
    LogStream& operator<<(const T* value)
    {
        mBuffer << value;
        return *this;
    }

    template<typename T, typename = typename std::enable_if<std::is_scalar<T>::value>::type>
    LogStream& operator<<(const T value)
    {
        mBuffer << value;
        return *this;
    }

    template<typename T, typename = typename std::enable_if<!std::is_scalar<T>::value>::type>
    LogStream& operator<<(const T& value)
    {
        mBuffer << value;
        return *this;
    }

private:
    std::ostringstream mBuffer;
}; // LogStream

extern std::string USER_AGENT;
extern bool gRunningInCI;
extern bool gTestingInvalidArgs;
extern bool gResumeSessions;
extern int gFseventsFd;

LogStream out();

enum { THREADS_PER_MEGACLIENT = 3 };

class TestingWithLogErrorAllowanceGuard
{
public:
    TestingWithLogErrorAllowanceGuard()
    {
        gTestingInvalidArgs = true;
    }
    ~TestingWithLogErrorAllowanceGuard()
    {
        gTestingInvalidArgs = false;
    }
};

class TestFS
{
public:
    // these getters should return std::filesystem::path type, when C++17 will become mandatory
    static fs::path GetTestBaseFolder();
    static fs::path GetTestFolder();
    static fs::path GetTrashFolder();

    void DeleteTestFolder() { DeleteFolder(GetTestFolder()); }
    void DeleteTrashFolder() { DeleteFolder(GetTrashFolder()); }

    ~TestFS();

private:
    void DeleteFolder(fs::path folder);

    std::vector<std::thread> m_cleaners;
};

void moveToTrash(const fs::path& p);
fs::path makeNewTestRoot();

template<class FsAccessClass>
FsAccessClass makeFsAccess_()
{
    return FsAccessClass(
#ifdef __APPLE__
                gFseventsFd
#endif
                );
}
