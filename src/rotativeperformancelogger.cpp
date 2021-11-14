/**
 * @file rotativeperformancelogger.cpp
 * @brief Logger class with log rotation and background write to file
 *
 * (c) 2013 by Mega Limited, Auckland, New Zealand
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
 *
 * This file is also distributed under the terms of the GNU General
 * Public License, see http://www.gnu.org/copyleft/gpl.txt for details.
 */

#include <cstring>
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

#include "megaapi_impl.h"
#include "mega/rotativeperformancelogger.h"

#ifdef WIN32
#include <windows.h>
#endif

#define MAX_MESSAGE_SIZE 4096

#define LOG_TIME_CHARS 25
#define LOG_LEVEL_CHARS 5

#define MAX_FILESIZE_MB 10    // 10MB of log usually compresses to about 850KB (was 450 before duplicate line detection)
#define MAX_ROTATE_LOGS 50   // So we expect to keep 42MB or so in compressed logs
#define MAX_ROTATE_LOGS_TODELETE 50   // If ever reducing the number of logs, we should remove the older ones anyway. This number should be the historical maximum of that value

#define SSTR( x ) static_cast< const std::ostringstream & >( \
        (  std::ostringstream() << std::dec << x ) ).str()

namespace mega
{

enum ArchiveType {archiveTypeNumbered, archiveTypeTimestamp};

using DirectLogFunction = std::function <void (std::ostream *)>;

struct LogLinkedList
{
    LogLinkedList* mNext = nullptr;
    unsigned mAllocated = 0;
    unsigned mUsed = 0;
    int mLastMessage = -1;
    int mLastMessageRepeats = 0;
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

class RotativePerformanceLoggerLoggingThread
{
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
    int mFlushOnLevel = MegaApi::LOG_LEVEL_WARNING;
    std::chrono::seconds mLogFlushPeriod = std::chrono::seconds(10);
    std::chrono::steady_clock::time_point mNextFlushTime = std::chrono::steady_clock::now() + mLogFlushPeriod;
    unique_ptr<MegaFileSystemAccess> mFsAccess;
    ArchiveType mArchiveType = archiveTypeTimestamp;
    long int archiveMaxFileAgeSeconds = 30 * 86400; // one month

    friend RotativePerformanceLogger;

public:
    RotativePerformanceLoggerLoggingThread() :
        mFsAccess(new MegaFileSystemAccess())
    {
    }

    void startLoggingThread(const LocalPath& logsPath, const LocalPath& fileName)
    {
        if (!mLogThread)
        {
            mLogThread.reset(new std::thread([this, logsPath, fileName]() {
                logThreadFunction(logsPath, fileName);
            }));
        }
    }

    void log(int loglevel, const char *message, const char **directMessages = nullptr, size_t *directMessagesSizes = nullptr, int numberMessages = 0);

    static void gzipCompressOnRotate(LocalPath localPath, LocalPath destinationLocalPath)
    {
        std::unique_ptr<MegaFileSystemAccess> fsAccess(new MegaFileSystemAccess());

        std::ifstream file(localPath.localpath.c_str(), std::ofstream::out);
        if (!file.is_open())
        {
            std::cerr << "Unable to open log file for reading: " << localPath.toPath(*fsAccess) << std::endl;
            return;
        }

        auto gzdeleter = [](gzFile_s* f) { if (f) gzclose(f); };
    #ifdef _WIN32
        std::unique_ptr<gzFile_s, decltype(gzdeleter)> gzfile{ gzopen_w(destinationLocalPath.localpath.c_str(), "wb"), gzdeleter};
    #else
        std::unique_ptr<gzFile_s, decltype(gzdeleter)> gzfile{ gzopen(destinationLocalPath.localpath.c_str(), "wb"), gzdeleter };
    #endif
        if (!gzfile)
        {
            std::cerr << "Unable to open gzfile for writing: " << localPath.toPath(*fsAccess) << std::endl;
            return;
        }

        std::string line;
        while (std::getline(file, line))
        {
            line.push_back('\n');
            if (gzputs(gzfile.get(), line.c_str()) == -1)
            {
                std::cerr << "Unable to compress log file: " << localPath.toPath(*fsAccess) << std::endl;
                return;
            }
        }

        fsAccess->unlinklocal(localPath);
    }

private:
    LocalPath logArchiveNumbered_getFilename(const LocalPath& baseFileName, int logNumber)
    {
        LocalPath newFileName = baseFileName;
        newFileName.append(LocalPath::fromPlatformEncoded("." + SSTR(logNumber) + ".gz"));
        return newFileName;
    }

    void logArchiveNumbered_cleanUpFiles(const LocalPath& logsPath, const LocalPath& fileName)
    {
        for (int i = MAX_ROTATE_LOGS_TODELETE; i--; )
        {
            LocalPath toDeleteFileName = logArchiveNumbered_getFilename(fileName, i);
            LocalPath toDeletePath = logsPath;
            toDeletePath.appendWithSeparator(toDeleteFileName, false);

            if (!mFsAccess->unlinklocal(toDeletePath))
            {
                std::cerr << "Error removing log file " << i << std::endl;
            }
        }
    }

    void logArchiveNumbered_rotateFiles(const LocalPath& logsPath, const LocalPath& fileName)
    {
        for (int i = MAX_ROTATE_LOGS_TODELETE; i--; )
        {
            LocalPath toRenameFileName = logArchiveNumbered_getFilename(fileName, i);
            LocalPath toRenamePath = logsPath;
            toRenamePath.appendWithSeparator(toRenameFileName, false);

            auto fileAccess = mFsAccess->newfileaccess();
            if (fileAccess->fopen(toRenamePath, true, false))
            {
                if (i + 1 >= MAX_ROTATE_LOGS)
                {
                    if (!mFsAccess->unlinklocal(toRenamePath))
                    {
                        std::cerr << "Error removing log file " << i << std::endl;
                    }
                }
                else
                {                   
                    LocalPath nextFileName = logArchiveNumbered_getFilename(fileName, i + 1);
                    LocalPath nextPath = logsPath;
                    nextPath.appendWithSeparator(nextFileName, false);
                    if (!mFsAccess->renamelocal(toRenamePath, nextPath, true))
                    {
                        std::cerr << "Error renaming log file " << i << std::endl;
                    }
                }
            }
        }
    }

    LocalPath logArchiveTimestamp_getFilename(const LocalPath& baseFileName)
    {
        std::time_t timestamp = std::time(nullptr);
        LocalPath newFileName = baseFileName;
        newFileName.append(LocalPath::fromPlatformEncoded("." + SSTR(timestamp) + ".gz"));
        return newFileName;
    }

    void logArchiveTimestamp_walkArchivedFiles(
            const LocalPath& logsPath, const LocalPath& fileName,
            const std::function< void(const LocalPath&, const LocalPath&) > & walker)
    {
        FileSystemType fsType = mFsAccess->getlocalfstype(logsPath);
         std::string logFileName = fileName.toName(*mFsAccess, fsType);
         if (!logFileName.empty())
         {
             LocalPath leafNamePath;
             DirAccess* da = mFsAccess->newdiraccess();
             nodetype_t dirEntryType;
             LocalPath logsPathCopy(logsPath);
             da->dopen(&logsPathCopy, NULL, false);
             while (da->dnext(logsPathCopy, leafNamePath, false, &dirEntryType))
             {
                 std::string leafName = leafNamePath.toName(*mFsAccess, fsType);
                 if (leafName.size() > logFileName.size())
                 {
                     auto res = std::mismatch(logFileName.begin(), logFileName.end(), leafName.begin());
                     if (res.first == logFileName.end())
                     { // logFileName is prefix of leafName
                         walker(logsPath, leafNamePath);
                     }
                 }
             }
             delete da;
         }
    }

    void logArchiveTimestamp_cleanUpFiles(const LocalPath& logsPath, const LocalPath& fileName)
    {
        logArchiveTimestamp_walkArchivedFiles(
                    logsPath, fileName,
                    [this](const LocalPath& logsPath, const LocalPath& leafNamePath)
        {
            LocalPath leafNameFullPath = logsPath;
            leafNameFullPath.appendWithSeparator(leafNamePath, false);
            if (!mFsAccess->unlinklocal(leafNameFullPath))
            {
                std::cerr << "Error removing log file " << leafNameFullPath.toPath(*mFsAccess) << std::endl;
            }
        });
    }

    void logArchiveTimestamp_rotateFiles(const LocalPath& logsPath, const LocalPath& fileName)
    {
        long int currentTimestamp = static_cast<long int> (std::time(nullptr));
        long int archiveMaxFileAgeSeconds = this->archiveMaxFileAgeSeconds;

        std::vector<std::pair<long int, LocalPath>> archivedTimestampsPathPairs;

        logArchiveTimestamp_walkArchivedFiles(
                    logsPath, fileName,
                    [this, currentTimestamp, archiveMaxFileAgeSeconds, &archivedTimestampsPathPairs](const LocalPath& logsPath,const LocalPath& leafNamePath)
        {
            std::string leafName = leafNamePath.toPath(*mFsAccess);
            std::regex rgx(".*\\.([0-9]+)\\.gz");
            std::smatch match;
            if (std::regex_match(leafName, match, rgx)
                    && match.size() == 2)
            {
                std::string leafTimestampString = match[1].str();
                long int leafTimestamp = std::stol(leafTimestampString);
                LocalPath leafNameFullPath = logsPath;
                leafNameFullPath.appendWithSeparator(leafNamePath, false);
                if (currentTimestamp - leafTimestamp > archiveMaxFileAgeSeconds)
                {
                    if (!mFsAccess->unlinklocal(leafNameFullPath))
                    {
                        std::cerr << "Error removing log file " << leafNameFullPath.toPath(*mFsAccess) << std::endl;
                    }
                }
                else
                {
                    archivedTimestampsPathPairs.push_back(std::make_pair(leafTimestamp, leafNameFullPath));
                }
            }
        });

        int extraFileNumber = static_cast<int>(archivedTimestampsPathPairs.size()) - MAX_ROTATE_LOGS;
        if (extraFileNumber > 0)
        {
            std::sort(archivedTimestampsPathPairs.begin(), archivedTimestampsPathPairs.end(),
                [](const std::pair<long int, LocalPath>& a, const std::pair<long int, LocalPath>& b)
                {
                    return a.first < b.first;
                }
            );

            for (auto &archivedTimestampsPathPair : archivedTimestampsPathPairs)
            {
                LocalPath& leafNameFullPathToDelete = archivedTimestampsPathPair.second;
                if (!mFsAccess->unlinklocal(leafNameFullPathToDelete))
                {
                    std::cerr << "Error removing log file " << leafNameFullPathToDelete.toPath(*mFsAccess) << std::endl;
                }
                else
                {
                    if (--extraFileNumber <= 0) break;
                }
            }
        }
    }

    LocalPath logArchive_getNewFilename(const LocalPath& fileName)
    {
        return mArchiveType == archiveTypeNumbered
                ? logArchiveNumbered_getFilename(fileName, 0)
                : logArchiveTimestamp_getFilename(fileName);
    }

    void logArchive_cleanUpFiles(const LocalPath& logsPath, const LocalPath& fileName)
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

    void logArchive_rotateFiles(const LocalPath& logsPath, const LocalPath& fileName)
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

    void logThreadFunction(LocalPath logsPath, LocalPath fileName)
    {
        LocalPath fileNameFullPath = logsPath;
        fileNameFullPath.appendWithSeparator(fileName, false);

        std::ofstream outputFile(fileNameFullPath.localpath.c_str(), std::ofstream::out | std::ofstream::app);

        outputFile << "----------------------------- program start -----------------------------\n";
        long long outFileSize = outputFile.tellp();

        while (!mLogExit)
        {
            if (mForceRenew)
            {
                std::lock_guard<std::mutex> g(mLogRotationMutex);
                logArchive_cleanUpFiles(logsPath, fileName);

                outputFile.close();


                if (!mFsAccess->unlinklocal(fileNameFullPath))
                {
                    std::cerr << "Error removing log file " << fileNameFullPath.toPath(*mFsAccess) << std::endl;
                }

                outputFile.open(fileNameFullPath.localpath.c_str(), std::ofstream::out);

                outFileSize = 0;

                mForceRenew = false;
            }
            else if (outFileSize > MAX_FILESIZE_MB*1024*1024)
            {
                std::lock_guard<std::mutex> g(mLogRotationMutex);
                logArchive_rotateFiles(logsPath, fileName);

                auto newNameDone = logsPath;
                newNameDone.appendWithSeparator(logArchive_getNewFilename(fileName), false);
                auto newNameZipping = newNameDone;
                newNameZipping.append(LocalPath::fromPlatformEncoded(".zipping"));

                outputFile.close();
                mFsAccess->unlinklocal(newNameZipping);
                mFsAccess->renamelocal(fileNameFullPath, newNameZipping, true);

                std::thread t([=]() {
                    std::lock_guard<std::mutex> g(mLogRotationMutex); // prevent another rotation while we work on this file
                    gzipCompressOnRotate(newNameZipping, newNameDone);
                });
                t.detach();

                outputFile.open(fileNameFullPath.localpath.c_str(), std::ofstream::out);
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

                if (RotativePerformanceLogger::Instance().mLogToStdout)
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
                if (RotativePerformanceLogger::Instance().mLogToStdout)
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

};


RotativePerformanceLogger::RotativePerformanceLogger()
{
}

RotativePerformanceLogger::~RotativePerformanceLogger()
{
    MegaApi::removeLoggerObject(this); // after this no more calls to RotativePerformanceLogger::log
    {
        std::lock_guard<std::mutex> g(mLoggingThread->mLogMutex);
        mLoggingThread->mLogExit = true;
        mLoggingThread->mLogConditionVariable.notify_one();
    }
    mLoggingThread->mLogThread->join();
    mLoggingThread->mLogThread.reset();
}

void RotativePerformanceLogger::initialize(const char * logsPath, const char * logFileName, bool logToStdout)
{
    auto logsPathLocalPath = LocalPath::fromPlatformEncoded(logsPath);
    auto logFileNameLocalPath = LocalPath::fromPlatformEncoded(logFileName);

    mLogToStdout = logToStdout;

    unique_ptr<MegaFileSystemAccess> fsAccess(new MegaFileSystemAccess());
    fsAccess->mkdirlocal(logsPathLocalPath, false, false);

    mLoggingThread.reset(new RotativePerformanceLoggerLoggingThread());
    mLoggingThread->startLoggingThread(logsPathLocalPath, logFileNameLocalPath);

    MegaApi::setLogLevel(MegaApi::LOG_LEVEL_MAX);
    MegaApi::addLoggerObject(this);
}

RotativePerformanceLogger& RotativePerformanceLogger::Instance() {
    static RotativePerformanceLogger myInstance;
    return myInstance;
}

void RotativePerformanceLogger::setArchiveNumbered()
{
    mLoggingThread->mArchiveType = archiveTypeNumbered;
}

void RotativePerformanceLogger::setArchiveTimestamps(long int maxFileAgeSeconds)
{
    mLoggingThread->mArchiveType = archiveTypeTimestamp;
    mLoggingThread->archiveMaxFileAgeSeconds = maxFileAgeSeconds;
}


class RotativePerformanceLoggerHelper
{
private:
    std::mutex mThreadNameMutex;
    std::map<std::thread::id, std::string> mThreadNames;
    struct tm mLastTm;
    time_t mLastT = 0;
    std::thread::id mLastThreadId;
    const char* mLastThreadName;

    RotativePerformanceLoggerHelper()
    {
    }

public:
    static RotativePerformanceLoggerHelper& Instance()
    {
        static RotativePerformanceLoggerHelper myInstance;
        return myInstance;
    }

    void cacheThreadNameAndTimeT(time_t t, struct tm& gmt, const char*& threadname)
    {
        std::lock_guard<std::mutex> g(mThreadNameMutex);

        if (t != mLastT)
        {
            mLastTm = *std::gmtime(&t);
            mLastT = t;
        }
        gmt = mLastTm;

        if (mLastThreadId == std::this_thread::get_id())
        {
            threadname = mLastThreadName;
            return;
        }

        auto& entry = mThreadNames[std::this_thread::get_id()];
        if (entry.empty())
        {
            std::ostringstream s;
            s << std::this_thread::get_id() << " ";
            entry = s.str();
        }
        threadname = mLastThreadName = entry.c_str();
        mLastThreadId = std::this_thread::get_id();
    }

    static inline void twodigit(char*& s, int n)
    {
        *s++ = static_cast<char>(n / 10 + '0');
        *s++ = static_cast<char>(n % 10 + '0');
    }

    static char* filltime(char* s, struct tm*  gmt, int microsec)
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
};

void RotativePerformanceLogger::log(const char*, int loglevel, const char*, const char *message
#ifdef ENABLE_LOG_PERFORMANCE
                         , const char **directMessages, size_t *directMessagesSizes, int numberMessages
#endif
                         )

{
    mLoggingThread->log(loglevel, message
#ifdef ENABLE_LOG_PERFORMANCE
                        , directMessages, directMessagesSizes, numberMessages
#endif
                        );
}

void RotativePerformanceLoggerLoggingThread::log(int loglevel, const char *message, const char **directMessages, size_t *directMessagesSizes, int numberMessages)
{
    bool direct = directMessages != nullptr;

    char timebuf[LOG_TIME_CHARS + 1];
    auto now = std::chrono::system_clock::now();
    time_t t = std::chrono::system_clock::to_time_t(now);

    struct tm gmt;
    const char* threadname;
    RotativePerformanceLoggerHelper::Instance().cacheThreadNameAndTimeT(t, gmt, threadname);

    auto microsec = std::chrono::duration_cast<std::chrono::microseconds>(now - std::chrono::system_clock::from_time_t(t));
    RotativePerformanceLoggerHelper::filltime(timebuf, &gmt, (int)microsec.count() % 1000000);

    const char* loglevelstring = "     ";
    switch (loglevel) // keeping these at 4 chars makes nice columns, easy to read
    {
    case MegaApi::LOG_LEVEL_FATAL: loglevelstring = "CRIT "; break;
    case MegaApi::LOG_LEVEL_ERROR: loglevelstring = "ERR  "; break;
    case MegaApi::LOG_LEVEL_WARNING: loglevelstring = "WARN "; break;
    case MegaApi::LOG_LEVEL_INFO: loglevelstring = "INFO "; break;
    case MegaApi::LOG_LEVEL_DEBUG: loglevelstring = "DBG  "; break;
    case MegaApi::LOG_LEVEL_MAX: loglevelstring = "DTL  "; break;
    }

    auto messageLen = strlen(message);
    auto threadnameLen = strlen(threadname);
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
                    DirectLogFunction func = [&timebuf, &threadname, &loglevelstring, &directMessages, &directMessagesSizes, numberMessages](std::ostream *oss)
                    {
                        *oss << timebuf << threadname << loglevelstring;

                        for(int i = 0; i < numberMessages; i++)
                        {
                            oss->write(directMessages[i], directMessagesSizes[i]);
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
                        mLogListLast->append(repeatbuf, n);
                    }
                    mLogListLast->append(timebuf, LOG_TIME_CHARS);
                    mLogListLast->append(threadname, unsigned(threadnameLen));
                    mLogListLast->append(loglevelstring, LOG_LEVEL_CHARS);
                    mLogListLast->mLastMessage = mLogListLast->mUsed;
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

bool RotativePerformanceLogger::cleanLogs()
{
    std::lock_guard<std::mutex> g(mLoggingThread->mLogMutex);
    mLoggingThread->mForceRenew = true;
    mLoggingThread->mLogConditionVariable.notify_one();
    return true;
}

void RotativePerformanceLogger::flushAndClose()
{
    try
    {
        mLoggingThread->log(MegaApi::LOG_LEVEL_FATAL, "***CRASH DETECTED: FLUSHING AND CLOSING***");

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

}
