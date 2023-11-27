/**
* @file Logger.cpp
* @brief Logger class with log rotation and background write to file
*
* (c) 2013 by Mega Limited, Auckland, New Zealand
*
* This file is part of the MEGAproxy.
*
* MEGAproxy is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
*
* @copyright Simplified (2-clause) BSD License.
*
* You should have received a copy of the license along with this
* program.
*/

#include <cstring>
#include <iomanip>
#include <string>
#include <fstream>
#include <iostream>
#include <sstream>
#include <ctime>
#include <regex>
#include <assert.h>
#include <map>
#include <chrono>
#include <thread>
#include <condition_variable>
#include <future>

#include <zlib.h>

#include "logger.h"
#include "megaapi_impl.h"
#include "mega/filesystem.h"
#include "mega/utils.h"

#ifdef WIN32
#include <windows.h>
#endif

#define LOG_TIME_CHARS 25
#define LOG_LEVEL_CHARS 5

#define LOG_FILE_NAME_EXTENSION ".gz"

#define SSTR( x ) static_cast< const std::ostringstream & >( \
        (  std::ostringstream() << std::dec << x ) ).str()

namespace mega
{
namespace gfx
{

namespace
{
#ifdef WIN32
std::wstring cstrFromLocalPath(const mega::LocalPath &localPath)
{
    std::wstring pathW;
    std::string path = localPath.toPath(false);
    mega::LocalPath::path2local(&path, &pathW);
    return pathW;
}
#else
std::string cstrFromLocalPath(const mega::LocalPath &localPath)
{
    return localPath.platformEncoded();
}
#endif
}

enum ArchiveType {archiveTypeNumbered, archiveTypeTimestamp};

using DirectLogFunction = std::function <void (std::ostream *)>;

struct LogLinkedList
{
    LogLinkedList* mNext = nullptr;
    unsigned mAllocated = 0;
    unsigned mUsed = 0;
    int mLastMessage = -1;
    unsigned int mLastMessageRepeats = 0;
    bool mOomGap = false;
    DirectLogFunction *mDirectLoggingFunction = nullptr; // we cannot use a non pointer due to the malloc allocation of new entries
    std::promise<void>* mCompletionPromise = nullptr; // we cannot use a unique_ptr due to the malloc allocation of new entries
    char mMessage[1];

    static LogLinkedList* create(LogLinkedList* prev, size_t size)
    {
        LogLinkedList* entry = (LogLinkedList*)malloc(size);
        if (entry)
        {
            entry->mNext = nullptr;
            entry->mAllocated = unsigned(size - sizeof(LogLinkedList));
            entry->mUsed = 0;
            entry->mLastMessage = -1;
            entry->mLastMessageRepeats = 0;
            entry->mOomGap = false;
            entry->mDirectLoggingFunction = nullptr;
            entry->mCompletionPromise = nullptr;
            prev->mNext = entry;
        }
        return entry;
    }

    bool messageFits(size_t size)
    {
        return mUsed + size + 2 < mAllocated;
    }

    bool needsDirectOutput()
    {
        return mDirectLoggingFunction != nullptr;
    }

    void append(const char* s, unsigned int n = 0)
    {
        n = n ? n : unsigned(strlen(s));
        assert(mUsed + n + 1 < mAllocated);
        strcpy(mMessage + mUsed, s);
        mUsed += n;
    }

    void notifyWaiter()
    {
        if (mCompletionPromise)
        {
            mCompletionPromise->set_value();
        }
    }

};

class MegaFileLoggerLoggingThread
{
    template <typename ExitCallback>
    class ScopeGuard
    {
        ExitCallback mExitCb;
    public:
        ScopeGuard(ExitCallback&& exitCb) : mExitCb{std::move(exitCb)} { }
        ~ScopeGuard() { mExitCb(); }
    };

    MegaFileLogger &mLogger;
    std::unique_ptr<std::thread> mLogThread;
    std::condition_variable mLogConditionVariable;
    std::mutex mLogMutex;
    std::mutex mLogRotationMutex;
    LogLinkedList mLogListFirst;
    LogLinkedList* mLogListLast = &mLogListFirst;
    bool mLogExit = false;
    bool mFlushLog = false;
    bool mCloseLog = false;
    bool mForceRenew = false; //to force removal of all logs and create an empty new log
    int mFlushOnLevel = mega::MegaApi::LOG_LEVEL_WARNING;
    std::chrono::seconds mLogFlushPeriod = std::chrono::seconds(10);
    std::chrono::steady_clock::time_point mNextFlushTime = std::chrono::steady_clock::now() + mLogFlushPeriod;
    std::unique_ptr<mega::MegaFileSystemAccess> mFsAccess;
    ArchiveType mArchiveType = archiveTypeTimestamp;
    std::atomic<std::chrono::seconds> mArchiveMaxFileAgeSeconds = std::chrono::seconds(30 * 86400); // one month
    std::atomic_int mMaxArchiveLogsToKeep = 50;
    std::atomic<size_t> mLogFileSize = 50 * 1024 * 1024;

    friend MegaFileLogger;

public:
    MegaFileLoggerLoggingThread(MegaFileLogger &logger)
        : mLogger(logger)
        , mFsAccess(new mega::MegaFileSystemAccess())
    {
    }

    ~MegaFileLoggerLoggingThread()
    {
        // if the gzip operation is onging, we can't destroy this object
        // yet, because this mutex is locked on that thread, and
        // on windows at least, we will crash.  So wait to lock it.
        std::lock_guard<std::mutex> g(mLogRotationMutex);
    }

    void startLoggingThread(const mega::LocalPath& logsPath, const mega::LocalPath& fileName)
    {
        if (!mLogThread)
        {
            mLogThread.reset(new std::thread([this, logsPath, fileName]() {
                logThreadFunction(logsPath, fileName);
            }));
        }
    }

    void log(int loglevel, const char *message, const char **directMessages = nullptr, size_t *directMessagesSizes = nullptr, int numberMessages = 0);

    static void gzipCompressOnRotate(mega::LocalPath source, mega::LocalPath destination)
    {
        auto sourcePlatformEncoded = cstrFromLocalPath(source);
        std::ifstream file(sourcePlatformEncoded.c_str(), std::ofstream::out);
        if (!file.is_open())
        {
            //EVLOG_err(LOGGER_COMPRESSING_LOGFILE_ON_ROTATION) << "Unable to open log file for reading: " << sourcePlatformEncoded;
            return;
        }

        auto destinationPlatformEncoded = cstrFromLocalPath(destination);
        auto gzdeleter = [](gzFile_s* f) { if (f) gzclose(f); };
    #ifdef _WIN32
        std::unique_ptr<gzFile_s, decltype(gzdeleter)> gzfile{ gzopen_w(destinationPlatformEncoded.c_str(), "wb"), gzdeleter};
    #else
        std::unique_ptr<gzFile_s, decltype(gzdeleter)> gzfile{ gzopen(destinationPlatformEncoded.c_str(), "wb"), gzdeleter };
    #endif
        if (!gzfile)
        {
            //EVLOG_err(LOGGER_COMPRESSING_LOGFILE_ON_ROTATION) << "Unable to open gzfile for writing: " << sourcePlatformEncoded;
            return;
        }

        std::string line;
        while (std::getline(file, line))
        {
            line.push_back('\n');
            if (gzputs(gzfile.get(), line.c_str()) == -1)
            {
                //EVLOG_err(LOGGER_COMPRESSING_LOGFILE_ON_ROTATION) << "Unable to compress log file: " << sourcePlatformEncoded;
                return;
            }
        }

        // We must release the open file handle or the unlink below will fail
        file.close();

        mega::MegaFileSystemAccess fsAccess;
        fsAccess.unlinklocal(source);
    }

private:
    mega::LocalPath logArchiveNumbered_getFilename(const mega::LocalPath& baseFileName, int logNumber)
    {
        mega::LocalPath newFileName = baseFileName;
        newFileName.append(mega::LocalPath::fromRelativePath("." + SSTR(logNumber) + LOG_FILE_NAME_EXTENSION));
        return newFileName;
    }

    void logArchiveNumbered_cleanUpFiles(const mega::LocalPath& logsPath, const mega::LocalPath& fileName)
    {
        for (int i = mMaxArchiveLogsToKeep - 1; i >= 0; --i)
        {
            mega::LocalPath toDeleteFileName = logArchiveNumbered_getFilename(fileName, i);
            mega::LocalPath toDeletePath = logsPath;
            toDeletePath.appendWithSeparator(toDeleteFileName, false);

            if (!mFsAccess->unlinklocal(toDeletePath))
            {
                //EVLOG_err(LOGGER_CLEANING_UP) << "Error removing log file " << i;
            }
        }
    }

    void logArchiveNumbered_rotateFiles(const mega::LocalPath& logsPath, const mega::LocalPath& fileName)
    {
        const int maxArchiveLogsToKeep = mMaxArchiveLogsToKeep;
        for (int i = maxArchiveLogsToKeep - 1; i >= 0; --i)
        {
            mega::LocalPath toRenameFileName = logArchiveNumbered_getFilename(fileName, i);
            mega::LocalPath toRenamePath = logsPath;
            toRenamePath.appendWithSeparator(toRenameFileName, false);

            auto fileAccess = mFsAccess->newfileaccess();
            if (fileAccess->fopen(toRenamePath, mega::FSLogging::logExceptFileNotFound))
            {
                if (i + 1 >= maxArchiveLogsToKeep)
                {
                    if (!mFsAccess->unlinklocal(toRenamePath))
                    {
                        //EVLOG_err(LOGGER_ROTATING_LOGFILES) << "Error removing log file " << i;
                    }
                }
                else
                {
                    mega::LocalPath nextFileName = logArchiveNumbered_getFilename(fileName, i + 1);
                    mega::LocalPath nextPath = logsPath;
                    nextPath.appendWithSeparator(nextFileName, false);
                    if (!mFsAccess->renamelocal(toRenamePath, nextPath, true))
                    {
                        //EVLOG_err(LOGGER_ROTATING_LOGFILES) << "Error renaming log file " << i;
                    }
                }
            }
        }
    }

    // note: this function can be removed after we have dropped support for the legacy timestamp format. (see logArchiveTimestamp_rotateFiles())
    std::string getTimeString(std::time_t timestamp, int64_t ms)
    {
        std::ostringstream oss;
        oss << std::put_time(std::localtime(&timestamp), "%y%m%d%H%M%S") << '.' << std::setfill('0') << std::setw(3) << ms;
        return oss.str();
    }
    std::string getTimeString(std::chrono::seconds offsetFromNowSec = std::chrono::seconds(0))
    {
        auto now = std::chrono::system_clock::now();
        std::time_t timestamp = std::chrono::system_clock::to_time_t(now + offsetFromNowSec);
        int64_t ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000;
        return getTimeString(timestamp, ms);
    }

    mega::LocalPath logArchiveTimestamp_getFilename(const mega::LocalPath& baseFileName)
    {
        mega::LocalPath newFileName = baseFileName;
        newFileName.append(mega::LocalPath::fromRelativePath("." + getTimeString() + LOG_FILE_NAME_EXTENSION));
        return newFileName;
    }

    void logArchiveTimestamp_walkArchivedFiles(
            const mega::LocalPath& logsPath, const mega::LocalPath& fileName,
            const std::function< void(const mega::LocalPath&, const mega::LocalPath&) > & walker)
    {
        std::string logFileName = fileName.toName(*mFsAccess);
        if (!logFileName.empty())
        {
            mega::LocalPath leafNamePath;
            auto da = mFsAccess->newdiraccess();
            mega::nodetype_t dirEntryType;
            mega::LocalPath logsPathCopy(logsPath);
            da->dopen(&logsPathCopy, NULL, false);
            while (da->dnext(logsPathCopy, leafNamePath, false, &dirEntryType))
            {
                std::string leafName = leafNamePath.toName(*mFsAccess);
                if (leafName.size() > logFileName.size())
                {
                    auto res = std::mismatch(logFileName.begin(), logFileName.end(), leafName.begin());
                    if (res.first == logFileName.end())
                    { // logFileName is prefix of leafName
                        walker(logsPath, leafNamePath);
                    }
                }
            }
        }
    }

    void logArchiveTimestamp_cleanUpFiles(const mega::LocalPath& logsPath, const mega::LocalPath& fileName)
    {
        logArchiveTimestamp_walkArchivedFiles(
                    logsPath, fileName,
                    [this](const mega::LocalPath& logsPath, const mega::LocalPath& leafNamePath)
        {
            mega::LocalPath leafNameFullPath = logsPath;
            leafNameFullPath.appendWithSeparator(leafNamePath, false);
            if (!mFsAccess->unlinklocal(leafNameFullPath))
            {
                //EVLOG_err(LOGGER_CLEANING_UP) << "Error removing log file " << leafNameFullPath;
            }
        });
    }

    void logArchiveTimestamp_rotateFiles(const mega::LocalPath& logsPath, const mega::LocalPath& fileName)
    {
        struct TsAndPath
        {
            double mTimestamp;
            mega::LocalPath mPath;
            bool operator>(const TsAndPath& other) const { return mTimestamp > other.mTimestamp; }
        };
        std::priority_queue<TsAndPath, std::vector<TsAndPath>, std::greater<TsAndPath>> archivedTimestampsPathPairs;

        const size_t fileNameLength = fileName.toPath(true).size();
        const size_t fileExtLength = strlen(LOG_FILE_NAME_EXTENSION);
        const double oldestTimeToKeep = std::stod(getTimeString(- mArchiveMaxFileAgeSeconds.load()));
        const int maxArchiveLogsToKeep = mMaxArchiveLogsToKeep;

        // 1. remove the logs that have a timestamp that is older than the "oldest to keep"
        logArchiveTimestamp_walkArchivedFiles(
                    logsPath, fileName,
                    [this, oldestTimeToKeep, maxArchiveLogsToKeep, &archivedTimestampsPathPairs, fileNameLength, fileExtLength]
                    (const mega::LocalPath& logsPath,const mega::LocalPath& leafNamePath)
        {
            std::string leafName = leafNamePath.toPath(true);
            if (leafName.substr(leafName.size() - fileExtLength) != LOG_FILE_NAME_EXTENSION ||  // matching extension
                leafName.size() <= (fileNameLength + 1 + fileExtLength))                        // leafName must be longer than filename + "." + extension
            {
                return;
            }
            // pick the part of 'yymmddHHMMSS.msec' from the leaf name
            std::string leafTimeStr = leafName.substr(fileNameLength + 1, leafName.size() - (fileNameLength + 1 + fileExtLength));

            // to be removed: try to compatible with the legacy timestamp format
            if (leafTimeStr.size() <= 10)
            {
                try
                {
                    int64_t timestamp = std::stoll(leafTimeStr);
                    leafTimeStr = getTimeString(timestamp, 0);
                }
                catch (...)
                {
                    return; // ignore non-supported log names
                }
            }

            double leafTime = 0;
            try
            {
                size_t leafTimeStrSize;
                leafTime = std::stod(leafTimeStr, &leafTimeStrSize);
                if (leafTimeStrSize != 16) // not a "yyyymmddHHMMSS.msec" number string
                {
                    throw;
                }
            }
            catch (...)
            {
                return; // ignore non-supported log names
            }

            mega::LocalPath leafNameFullPath = logsPath;
            leafNameFullPath.appendWithSeparator(leafNamePath, false);
            if (leafTime < oldestTimeToKeep || !maxArchiveLogsToKeep)
            {
                if (!mFsAccess->unlinklocal(leafNameFullPath))
                {
                    //EVLOG_err(LOGGER_CLEANING_UP) << "Error removing log file " << leafNameFullPath;
                }
            }
            else if (maxArchiveLogsToKeep > 0)
            {
                archivedTimestampsPathPairs.push({leafTime, leafNameFullPath});
            }
        });

        // 2. remove the oldest log until total logs < max logs to keep
        // (because rotating logs happens before creating the new log, so here we only needs to keep max-1 logs)
        if (maxArchiveLogsToKeep > 0)
        {
            while (archivedTimestampsPathPairs.size() >= (size_t)maxArchiveLogsToKeep)
            {
                const TsAndPath &oldest = archivedTimestampsPathPairs.top();
                const mega::LocalPath &leafNameFullPathToDelete = oldest.mPath;
                if (!mFsAccess->unlinklocal(leafNameFullPathToDelete))
                {
                    //EVLOG_err(LOGGER_CLEANING_UP) << "Error removing log file " << leafNameFullPathToDelete;
                }
                archivedTimestampsPathPairs.pop();
            }
        }
    }

    mega::LocalPath logArchive_getNewFilename(const mega::LocalPath& fileName)
    {
        return mArchiveType == archiveTypeNumbered
                ? logArchiveNumbered_getFilename(fileName, 0)
                : logArchiveTimestamp_getFilename(fileName);
    }

    void logArchive_cleanUpFiles(const mega::LocalPath& logsPath, const mega::LocalPath& fileName)
    {
        if (mArchiveType == archiveTypeNumbered)
        {
            logArchiveNumbered_cleanUpFiles(logsPath, fileName);
        }
        else
        {
            logArchiveTimestamp_cleanUpFiles(logsPath, fileName);
        }
    }

    void logArchive_rotateFiles(const mega::LocalPath& logsPath, const mega::LocalPath& fileName)
    {
        if (mArchiveType == archiveTypeNumbered)
        {
            logArchiveNumbered_rotateFiles(logsPath, fileName);
        }
        else
        {
            logArchiveTimestamp_rotateFiles(logsPath, fileName);
        }
    }

    void logThreadFunction(mega::LocalPath logsPath, mega::LocalPath fileName)
    {
        MegaFileLogger::setThreadName("LoggerMain");
        // Avoid cycles and possible deadloks - no logging from this log output thread.
        mega::SimpleLogger::mThreadLocalLoggingDisabled = true;

        // Error messages from this thread will be output directly to file
        // they are collected here
        std::string threadErrors;

        mega::LocalPath fileNameFullPath = logsPath;
        fileNameFullPath.appendWithSeparator(fileName, false);

        auto fileNameFullPathPlatformEncoded = cstrFromLocalPath(fileNameFullPath);
        std::ofstream outputFile(fileNameFullPathPlatformEncoded.c_str(), std::ofstream::out | std::ofstream::app);

        outputFile << "----------------------------- program start -----------------------------\n";
        int64_t outFileSize = outputFile.tellp();

        // Auxiliary thread used for zipping in the background:
        std::atomic_bool zippingThreadExit;
        zippingThreadExit.store(false);

        std::mutex zippingQueueMutex;
        std::deque<mega::LocalPath> zippingQueueFiles;
        std::condition_variable zippingWakeCv;

        std::thread zippingThread([&](){
            MegaFileLogger::setThreadName("LoggerZipping");
            mega::LocalPath newNameDone;

            while(true)
            {
                {
                    std::unique_lock<std::mutex> lock(zippingQueueMutex);
                    zippingWakeCv.wait(lock, [&](){ return zippingThreadExit.load() || !zippingQueueFiles.empty();});
                    if (zippingQueueFiles.empty()) // Let it deplete the queue and zip all the pending ones before exiting
                    {
                        assert(zippingThreadExit.load());
                        return;
                    }
                    newNameDone = std::move(zippingQueueFiles.front());
                    zippingQueueFiles.pop_front();
                }
                {   //do zip
                    auto newNameZipping = newNameDone;
                    newNameZipping.append(mega::LocalPath::fromRelativePath(".zipping"));

                    std::lock_guard<std::mutex> g(mLogRotationMutex); // Ensure no concurrency issue with while rotating thread regarding cleanups/.zipping file removals
                    gzipCompressOnRotate(newNameZipping, newNameDone);
                }
            }
        });
        // Ensure we finish and wait for zipping thread
        ScopeGuard<std::function<void(void)>> g([&](){
            zippingThreadExit.store(true);
            zippingWakeCv.notify_one();
            zippingThread.join();
        });

        auto pushToZippingThread = [&](mega::LocalPath &&newNameDone)
        {
            {
                std::lock_guard<std::mutex> g(zippingQueueMutex);
                zippingQueueFiles.push_back(std::move(newNameDone));
            }
            zippingWakeCv.notify_one();
        };

        while (!mLogExit)
        {
            if (!threadErrors.empty())
            {
                outputFile  << threadErrors << std::endl;
                threadErrors.clear();
            }

            if (mForceRenew)
            {
                std::lock_guard<std::mutex> g(mLogRotationMutex);
                logArchive_cleanUpFiles(logsPath, fileName);

                outputFile.close();


                if (!mFsAccess->unlinklocal(fileNameFullPath))
                {
                    threadErrors += "Error removing log file " + fileNameFullPath.toPath(true) + "\n";
                }

                outputFile.open(fileNameFullPathPlatformEncoded.c_str(), std::ofstream::out);

                outFileSize = 0;

                mForceRenew = false;
            }
            else if (outFileSize > (int64_t)mLogFileSize.load())
            {
                std::lock_guard<std::mutex> g(mLogRotationMutex);
                logArchive_rotateFiles(logsPath, fileName);
                outputFile.close();

                if (mMaxArchiveLogsToKeep > 0)
                {
                    auto newNameDone = logsPath;
                    newNameDone.appendWithSeparator(logArchive_getNewFilename(fileName), false);
                    auto newNameZipping = newNameDone;
                    newNameZipping.append(mega::LocalPath::fromRelativePath(".zipping"));

                    // Ensure there does not exist a clashing .zipping file:
                    if (!mFsAccess->unlinklocal(newNameZipping) && errno != ENOENT /*ignore if file not exist*/)
                    {
                        threadErrors += "Failed to unlink log file: " + newNameZipping.toPath(true) + "\n";
                    }
                    // rename to .zipping and queue the zipping into the zipping thread:
                    if (mFsAccess->renamelocal(fileNameFullPath, newNameZipping, true))
                    {
                        pushToZippingThread(std::move(newNameDone));
                    }
                    else
                    {
                        threadErrors += "Failed to rename log file: " + fileNameFullPath.toPath(true) + " to " + newNameZipping.toPath(true) + "\n";
                    }
                }

                outputFile.open(fileNameFullPathPlatformEncoded.c_str(), std::ofstream::out);
                outFileSize = 0;
            }

            LogLinkedList* newMessages = nullptr;
            bool topLevelMemoryGap = false;
            {
                std::unique_lock<std::mutex> lock(mLogMutex);
                mLogConditionVariable.wait_for(lock, std::chrono::milliseconds(500), [this, &newMessages, &topLevelMemoryGap]() {
                        if (mForceRenew || mLogListFirst.mNext || mLogExit || mFlushLog || mCloseLog)
                        {
                            newMessages = mLogListFirst.mNext;
                            mLogListFirst.mNext = nullptr;
                            mLogListLast = &mLogListFirst;
                            topLevelMemoryGap = mLogListFirst.mOomGap;
                            mLogListFirst.mOomGap = false;
                            return true;
                        }
                        else return false;
                });
            }

            if (topLevelMemoryGap)
            {
                if (outputFile)
                {
                    outputFile << "<log gap - out of logging memory at this point>\n";
                }
            }

            while (newMessages)
            {
                auto p = newMessages;
                newMessages = newMessages->mNext;
                if (outputFile)
                {
                    if (p->needsDirectOutput())
                    {
                        (*p->mDirectLoggingFunction)(&outputFile);
                    }
                    else
                    {
                        outputFile << p->mMessage;
                        outFileSize += p->mUsed;
                        if (p->mOomGap)
                        {
                            outputFile << "<log gap - out of logging memory at this point>\n";
                        }
                    }
                }

                if (mLogger.mLogToStdout)
                {
                    if (p->needsDirectOutput())
                    {
                        (*p->mDirectLoggingFunction)(&std::cout);
                    }
                    else
                    {
                        std::cout << p->mMessage;
                    }
                    std::cout << std::flush; //always flush into stdout (DEBUG mode)
                }
                p->notifyWaiter();
                free(p);
            }
            if (mFlushLog || mNextFlushTime <= std::chrono::steady_clock::now())
            {
                mFlushLog = false;
                outputFile.flush();
                if (mLogger.mLogToStdout)
                {
                    std::cout << std::flush;
                }
                mNextFlushTime = std::chrono::steady_clock::now() + mLogFlushPeriod;
            }

            if (mCloseLog)
            {
                outputFile.close();
                return;  // This request means we have received a termination signal; close and exit the thread as quick & clean as possible
            }
        }
    }

    static std::string currentThreadName()
    {
        std::ostringstream s;
        s << std::this_thread::get_id() << " ";
        return s.str();
    }

    static char* filltime(char *s, struct tm *gmt, int microsec)
    {
        twodigit(s, gmt->tm_mday);
        *s++ = '/';
        twodigit(s, gmt->tm_mon + 1);
        *s++ = '/';
        twodigit(s, gmt->tm_year % 100);
        *s++ = '-';
        twodigit(s, gmt->tm_hour);
        *s++ = ':';
        twodigit(s, gmt->tm_min);
        *s++ = ':';
        twodigit(s, gmt->tm_sec);
        *s++ = '.';
        s[5] = static_cast<char>(microsec % 10 + '0');
        s[4] = static_cast<char>((microsec /= 10) % 10 + '0');
        s[3] = static_cast<char>((microsec /= 10) % 10 + '0');
        s[2] = static_cast<char>((microsec /= 10) % 10 + '0');
        s[1] = static_cast<char>((microsec /= 10) % 10 + '0');
        s[0] = static_cast<char>((microsec /= 10) % 10 + '0');
        s += 6;
        *s++ = ' ';
        *s = 0;
        return s;
    }

    static inline void twodigit(char *&s, int n)
    {
        *s++ = static_cast<char>(n / 10 + '0');
        *s++ = static_cast<char>(n % 10 + '0');
    }
};

thread_local std::string MegaFileLogger::sThreadName = {};

#ifdef DEBUG
const std::string MegaFileLogger::sDefaultLogLevelStr{"max"};
#else
const std::string MegaFileLogger::sDefaultLogLevelStr{"debug"};
#endif

MegaFileLogger::MegaFileLogger() :
    mLogLevelStringToEnumMap({
        #define GENERATOR_MACRO(Enum, String) { String, Enum },
          GENERATE_LOG_LEVELS_FROM_CFG_STRING
        #undef GENERATOR_MACRO
    }),
    mInited(false)
{
}

MegaFileLogger::~MegaFileLogger()
{
    stopLogger();
}

void MegaFileLogger::stopLogger()
{
    if (!mInited) return;
    // note: possible race/crash here if there are any other threads calling log()
    // this mLoggingThread is about to be deleted, and the currently
    // logging threads call into it
    mega::MegaApi::removeLoggerObject(this, true);

    {
        std::lock_guard<std::mutex> g(mLoggingThread->mLogMutex);
        mLoggingThread->mLogExit = true;
        mLoggingThread->mLogConditionVariable.notify_one();
    }

    if (mLoggingThread->mLogThread)
    {
        mLoggingThread->mLogThread->join();
        mLoggingThread->mLogThread.reset();
    }
}

void MegaFileLogger::initialize(const char * logsPath, const char * logFileName, bool logToStdout)
{
    auto logsPathLocalPath = mega::LocalPath::fromAbsolutePath(logsPath);
    auto logFileNameLocalPath = mega::LocalPath::fromRelativePath(logFileName);

    mLogToStdout = logToStdout;

    std::unique_ptr<mega::MegaFileSystemAccess> fsAccess(new mega::MegaFileSystemAccess());
    fsAccess->mkdirlocal(logsPathLocalPath, false, false);

    if (mLoggingThread)
    {
        stopLogger();
    }
    // note:  probable crash here if other threads are currently logging
    // since we are about to delete the mLoggingThread out from under them
    // (stopLogger and MegaApi::removeLoggerObject do not lock, so have
    // no idea whether anything has currently called into RPL)
    mLoggingThread.reset(new MegaFileLoggerLoggingThread(*this));
    mLoggingThread->startLoggingThread(logsPathLocalPath, logFileNameLocalPath);

    mega::MegaApi::setLogLevel(logLevelFromString(sDefaultLogLevelStr));
    mega::MegaApi::addLoggerObject(this, true);
    mInited = true;
}

void MegaFileLogger::setArchiveNumbered()
{
    if (!mInited) return;
    mLoggingThread->mArchiveType = archiveTypeNumbered;
}

void MegaFileLogger::setMaxArchiveAge(std::chrono::seconds maxAgeSeconds)
{
    if (!mInited) return;
    mLoggingThread->mArchiveType = archiveTypeTimestamp;
    mLoggingThread->mArchiveMaxFileAgeSeconds = maxAgeSeconds;
}

void MegaFileLogger::setMaxArchivesToKeep(int maxArchives)
{
    if (!mInited) return;
    mLoggingThread->mMaxArchiveLogsToKeep = maxArchives;
}

void MegaFileLogger::setLogFileSize(size_t size)
{
    if (!mInited) return;
    mLoggingThread->mLogFileSize = size;
}

void MegaFileLogger::log(const char*, int loglevel, const char*, const char *message
#ifdef ENABLE_LOG_PERFORMANCE
                         , const char **directMessages, size_t *directMessagesSizes, int numberMessages
#endif
                         )

{
    if (!mInited) return;
    mLoggingThread->log(loglevel, message
#ifdef ENABLE_LOG_PERFORMANCE
                        , directMessages, directMessagesSizes, numberMessages
#endif
                        );
}

void MegaFileLoggerLoggingThread::log(int loglevel, const char *message, const char **directMessages, size_t *directMessagesSizes, int numberMessages)
{
    bool direct =
#ifdef ENABLE_LOG_PERFORMANCE
            directMessages != nullptr;
#else
            false;
#endif

    char timebuf[LOG_TIME_CHARS + 1];
    auto now = std::chrono::system_clock::now();
    time_t t = std::chrono::system_clock::to_time_t(now);
    struct tm gmt;
    memset(&gmt, 0, sizeof(struct tm));
    mega::m_gmtime(t, &gmt);

    static thread_local std::string threadname = currentThreadName();
    if (!MegaFileLogger::sThreadName.empty())
    {
        threadname = std::string(std::move(MegaFileLogger::sThreadName)) + " ";
    }

    auto microsec = std::chrono::duration_cast<std::chrono::microseconds>(now - std::chrono::system_clock::from_time_t(t));
    filltime(timebuf, &gmt, (int)microsec.count() % 1000000);

    const char* loglevelstring = "     ";
    switch (loglevel) // keeping these at 4 chars makes nice columns, easy to read
    {
    case mega::MegaApi::LOG_LEVEL_FATAL: loglevelstring = "CRIT "; break;
    case mega::MegaApi::LOG_LEVEL_ERROR: loglevelstring = "ERR  "; break;
    case mega::MegaApi::LOG_LEVEL_WARNING: loglevelstring = "WARN "; break;
    case mega::MegaApi::LOG_LEVEL_INFO: loglevelstring = "INFO "; break;
    case mega::MegaApi::LOG_LEVEL_DEBUG: loglevelstring = "DBG  "; break;
    case mega::MegaApi::LOG_LEVEL_MAX: loglevelstring = "DTL  "; break;
    }

    auto messageLen = strlen(message);
    auto threadnameLen = threadname.size();
    auto lineLen = LOG_TIME_CHARS + threadnameLen + LOG_LEVEL_CHARS + messageLen;
    bool notify = false;

    {
        std::unique_ptr<std::lock_guard<std::mutex>> g(new std::lock_guard<std::mutex>(mLogMutex));

        bool isRepeat = !direct && mLogListLast != &mLogListFirst &&
                        mLogListLast->mLastMessage >= 0 &&
                        !strncmp(message, mLogListLast->mMessage + mLogListLast->mLastMessage, messageLen);

        if (isRepeat)
        {
            ++mLogListLast->mLastMessageRepeats;
        }
        else
        {
            unsigned reportRepeats = mLogListLast != &mLogListFirst ? mLogListLast->mLastMessageRepeats : 0;
            if (reportRepeats)
            {
                lineLen += 30;
                mLogListLast->mLastMessageRepeats = 0;
            }

            if (direct)
            {
                if (LogLinkedList* newentry = LogLinkedList::create(mLogListLast, 1 + sizeof(LogLinkedList))) //create a new "empty" element
                {
                    mLogListLast = newentry;
                    std::promise<void> promise;
                    mLogListLast->mCompletionPromise = &promise;
                    auto future = mLogListLast->mCompletionPromise->get_future();
                    auto threadnameCStr = threadname.c_str();
                    DirectLogFunction func = [&timebuf, threadnameCStr, &loglevelstring, &directMessages, &directMessagesSizes, numberMessages](std::ostream *oss)
                    {
                        *oss << timebuf << threadnameCStr << loglevelstring;

                        for(int i = 0; i < numberMessages; i++)
                        {
                            oss->write(directMessages[i], static_cast<std::streamsize>(directMessagesSizes[i]));
                        }
                        *oss << std::endl;
                    };

                    mLogListLast->mDirectLoggingFunction = &func;

                    g.reset(); //to liberate the mutex and let the logging thread call the logging function

                    mLogConditionVariable.notify_one();

                    //wait for until logging thread completes the outputting
                    future.get();
                    return;
                }
                else
                {
                    mLogListLast->mOomGap = true;
                }
            }
            else
            {
                if (mLogListLast == &mLogListFirst || mLogListLast->mOomGap || !mLogListLast->messageFits(lineLen))
                {
                    if (LogLinkedList* newentry = LogLinkedList::create(mLogListLast, std::max<size_t>(lineLen, 8192) + sizeof(LogLinkedList) + 10))
                    {
                        mLogListLast = newentry;
                    }
                    else
                    {
                        mLogListLast->mOomGap = true;
                    }
                }
                if (!mLogListLast->mOomGap)
                {
                    if (reportRepeats)
                    {
                        char repeatbuf[31]; // this one can occur very frequently with many in a row: cURL DEBUG: schannel: failed to decrypt data, need more data
                        int n = snprintf(repeatbuf, 30, "[repeated x%u]\n", reportRepeats);
                        assert(n && "Unexpected snprintf failure");
                        if (n > 0)
                        {
                            mLogListLast->append(repeatbuf, static_cast<unsigned int>(n));
                        }
                    }
                    mLogListLast->append(timebuf, LOG_TIME_CHARS);
                    mLogListLast->append(threadname.c_str(), unsigned(threadnameLen));
                    mLogListLast->append(loglevelstring, LOG_LEVEL_CHARS);
                    mLogListLast->mLastMessage = static_cast<int>(mLogListLast->mUsed);
                    mLogListLast->append(message, unsigned(messageLen));
                    mLogListLast->append("\n", 1);
                    notify = mLogListLast->mUsed + 1024 > mLogListLast->mAllocated;
                }
            }
        }

        if (loglevel <= mFlushOnLevel)
        {
            mFlushLog = true;
        }
    }

    if (notify)
    {
        // notify outside the mutex lock is better (and correct) for much less chance the other
        // thread wakes up just to find the mutex locked. (saw lower cpu on the other thread like this)
        // Still, this notify call was taking 1% when notifying on every log line, so let the other thead
        // wake up by itself every 500ms without notify for the common case.
        // But still wake it if our memory block is getting full
        mLogConditionVariable.notify_one();
    }
}

bool MegaFileLogger::cleanLogs()
{
    if (!mInited) return false;
    std::lock_guard<std::mutex> g(mLoggingThread->mLogMutex);
    mLoggingThread->mForceRenew = true;
    mLoggingThread->mLogConditionVariable.notify_one();
    return true;
}

void MegaFileLogger::flush()
{
    if (!mInited) return;
    std::lock_guard<std::mutex> g(mLoggingThread->mLogMutex);
    mLoggingThread->mFlushLog = true;
    mLoggingThread->mLogConditionVariable.notify_one();
}

void MegaFileLogger::flushAndClose()
{
    if (!mInited) return;
    try
    {
        mLoggingThread->log(mega::MegaApi::LOG_LEVEL_FATAL, "***CRASH DETECTED: FLUSHING AND CLOSING***");

    }
    catch (const std::exception& e)
    {
        std::cerr << "Unhandle exception on flushAndClose: "<< e.what() << std::endl;
    }
    mLoggingThread->mFlushLog = true;
    mLoggingThread->mCloseLog = true;
    mLoggingThread->mLogConditionVariable.notify_one();
    // This is called on crash so the app may be unstable. Don't assume the thread is working properly.
    // It might be the one that crashed.  Just give it 1 second to complete
#ifdef WIN32
    Sleep(1000);
#else
    usleep(1000000);
#endif
}

int MegaFileLogger::logLevelFromString(const std::string &logLevelStr)
{
    // have lower case copy
    auto levelStr = logLevelStr;
    std::transform(std::begin(levelStr),
                   std::end(levelStr),
                   std::begin(levelStr),
                   [](unsigned char c) { return std::tolower(c); });

    auto it = mLogLevelStringToEnumMap.find(levelStr);
    if (it == mLogLevelStringToEnumMap.end())
    {
        return -1;
    }

    return it->second;
}

void MegaFileLogger::setLogLevel(const std::string &str)
{
    auto level = logLevelFromString(str);
    if (level == -1) {
        assert(false && "Invalid log level string");
        level = static_cast<int>(logLevelFromString(sDefaultLogLevelStr));
    }

    mega::MegaApi::setLogLevel(level);
}

void MegaFileLogger::setLogLevel(int level)
{
    mega::MegaApi::setLogLevel(level);
}

MegaFileLogger& MegaFileLogger::get()
{
    static MegaFileLogger logger;

    return logger;
}

}
}