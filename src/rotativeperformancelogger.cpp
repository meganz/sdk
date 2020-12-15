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

#include "mega/rotativeperformancelogger.h"

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
    LogLinkedList* next = nullptr;
    unsigned allocated = 0;
    unsigned used = 0;
    int lastmessage = -1;
    int lastmessageRepeats = 0;
    bool oomGap = false;
    DirectLogFunction *mDirectLoggingFunction = nullptr; // we cannot use a non pointer due to the malloc allocation of new entries
    std::promise<void>* mCompletionPromise = nullptr; // we cannot use a unique_ptr due to the malloc allocation of new entries
    char message[1];

    static LogLinkedList* create(LogLinkedList* prev, size_t size)
    {
        LogLinkedList* entry = (LogLinkedList*)malloc(size);
        if (entry)
        {
            entry->next = nullptr;
            entry->allocated = unsigned(size - sizeof(LogLinkedList));
            entry->used = 0;
            entry->lastmessage = -1;
            entry->lastmessageRepeats = 0;
            entry->oomGap = false;
            entry->mDirectLoggingFunction = nullptr;
            entry->mCompletionPromise = nullptr;
            prev->next = entry;
        }
        return entry;
    }

    bool messageFits(size_t size)
    {
        return used + size + 2 < allocated;
    }

    bool needsDirectOutput()
    {
        return mDirectLoggingFunction != nullptr;
    }

    void append(const char* s, unsigned int n = 0)
    {
        n = n ? n : unsigned(strlen(s));
        assert(used + n + 1 < allocated);
        strcpy(message + used, s);
        used += n;
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
    std::unique_ptr<std::thread> logThread;
    std::condition_variable logConditionVariable;
    std::mutex logMutex;
    std::mutex logRotationMutex;
    LogLinkedList logListFirst;
    LogLinkedList* logListLast = &logListFirst;
    bool logExit = false;
    bool flushLog = false;
    bool closeLog = false;
    bool forceRenew = false; //to force removal of all logs and create an empty MEGAsync.log
    int flushOnLevel = MegaApi::LOG_LEVEL_WARNING;
    std::chrono::seconds logFlushPeriod = std::chrono::seconds(10);
    std::chrono::steady_clock::time_point nextFlushTime = std::chrono::steady_clock::now() + logFlushPeriod;
    MegaFileSystemAccess * fsAccess;
    ArchiveType archiveType = archiveTypeTimestamp;
    long int archiveMaxFileAgeSeconds = 30 * 86400; // one month

    friend RotativePerformanceLogger;

public:
    RotativePerformanceLoggerLoggingThread()
    {
        fsAccess = new MegaFileSystemAccess();
    }

    ~RotativePerformanceLoggerLoggingThread()
    {
        delete fsAccess;
    }

    void startLoggingThread(const LocalPath& logsPath, const LocalPath& fileName)
    {
        if (!logThread)
        {
            logThread.reset(new std::thread([this, logsPath, fileName]() {
                logThreadFunction(logsPath, fileName);
            }));
        }
    }

    void log(int loglevel, const char *message, const char **directMessages = nullptr, size_t *directMessagesSizes = nullptr, int numberMessages = 0);

    static void gzipCompressOnRotate(LocalPath localPath, LocalPath destinationLocalPath)
    {
        MegaFileSystemAccess * fsAccess = new MegaFileSystemAccess();

        std::ifstream file(localPath.localpath.c_str(), std::ofstream::out);
        if (!file.is_open())
        {
            std::cerr << "Unable to open log file for reading: " << localPath.toPath(*fsAccess) << std::endl;
            delete fsAccess;
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
            delete fsAccess;
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
        delete fsAccess;
    }

private:
    LocalPath logArchiveNumbered_getFilename(LocalPath baseFileName, int logNumber)
    {
        LocalPath newFileName = baseFileName;
        newFileName.append(LocalPath::fromPlatformEncoded("." + SSTR(logNumber) + ".gz"));
        return newFileName;
    }

    void logArchiveNumbered_cleanUpFiles(LocalPath logsPath, LocalPath fileName)
    {
        for (int i = MAX_ROTATE_LOGS_TODELETE; i--; )
        {
            LocalPath toDeleteFileName = logArchiveNumbered_getFilename(fileName, i);
            LocalPath toDeletePath = logsPath;
            toDeletePath.appendWithSeparator(toDeleteFileName, false);

            if (!fsAccess->unlinklocal(toDeletePath))
            {
                std::cerr << "Error removing log file " << i << std::endl;
            }
        }
    }

    void logArchiveNumbered_rotateFiles(LocalPath logsPath, LocalPath fileName)
    {
        for (int i = MAX_ROTATE_LOGS_TODELETE; i--; )
        {
            LocalPath toRenameFileName = logArchiveNumbered_getFilename(fileName, i);
            LocalPath toRenamePath = logsPath;
            toRenamePath.appendWithSeparator(toRenameFileName, false);

            auto fileAccess = fsAccess->newfileaccess();
            if (fileAccess->fopen(toRenamePath, true, false))
            {
                if (i + 1 >= MAX_ROTATE_LOGS)
                {
                    if (!fsAccess->unlinklocal(toRenamePath))
                    {
                        std::cerr << "Error removing log file " << i << std::endl;
                    }
                }
                else
                {                   
                    LocalPath nextFileName = logArchiveNumbered_getFilename(fileName, i + 1);
                    LocalPath nextPath = logsPath;
                    nextPath.appendWithSeparator(nextFileName, false);
                    if (!fsAccess->renamelocal(toRenamePath, nextPath, true))
                    {
                        std::cerr << "Error renaming log file " << i << std::endl;
                    }
                }
            }
        }
    }

    LocalPath logArchiveTimestamp_getFilename(LocalPath baseFileName)
    {
        std::time_t timestamp = std::time(nullptr);
        LocalPath newFileName = baseFileName;
        newFileName.append(LocalPath::fromPlatformEncoded("." + SSTR(timestamp) + ".gz"));
        return newFileName;
    }

    void logArchiveTimestamp_walkArchivedFiles(
            LocalPath logsPath, LocalPath fileName,
            const std::function< void(LocalPath, LocalPath) > & walker)
    {
        FileSystemType fsType = fsAccess->getlocalfstype(logsPath);
         std::string logFileName = fileName.toName(*fsAccess, fsType);
         if (!logFileName.empty())
         {
             LocalPath leafNamePath;
             DirAccess* da = fsAccess->newdiraccess();
             nodetype_t dirEntryType;
             da->dopen(&logsPath, NULL, false);
             while (da->dnext(logsPath, leafNamePath, false, &dirEntryType))
             {
                 std::string leafName = leafNamePath.toName(*fsAccess, fsType);
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

    void logArchiveTimestamp_cleanUpFiles(LocalPath logsPath, LocalPath fileName)
    {
        logArchiveTimestamp_walkArchivedFiles(
                    logsPath, fileName,
                    [this](const LocalPath& logsPath,const LocalPath& leafNamePath)
        {
            LocalPath leafNameFullPath = logsPath;
            leafNameFullPath.appendWithSeparator(leafNamePath, false);
            if (!fsAccess->unlinklocal(leafNameFullPath))
            {
                std::cerr << "Error removing log file " << leafNameFullPath.toPath(*fsAccess) << std::endl;
            }
        });
    }

    void logArchiveTimestamp_rotateFiles(LocalPath logsPath, LocalPath fileName)
    {
        long int currentTimestamp = static_cast<long int> (std::time(nullptr));
        long int archiveMaxFileAgeSeconds = this->archiveMaxFileAgeSeconds;

        logArchiveTimestamp_walkArchivedFiles(
                    logsPath, fileName,
                    [this, currentTimestamp, archiveMaxFileAgeSeconds](const LocalPath& logsPath,const LocalPath& leafNamePath)
        {
            std::string leafName = leafNamePath.toPath(*fsAccess);
            std::regex rgx(".*\\.([0-9]+)\\.gz");
            std::smatch match;
            if (std::regex_match(leafName, match, rgx)
                    && match.size() == 2)
            {
                std::string leafTimestampString = match[1].str();
                long int leafTimestamp = std::stol(leafTimestampString);
                if (currentTimestamp - leafTimestamp > archiveMaxFileAgeSeconds)
                {
                    LocalPath leafNameFullPath = logsPath;
                    leafNameFullPath.appendWithSeparator(leafNamePath, false);
                    if (!fsAccess->unlinklocal(leafNameFullPath))
                    {
                        std::cerr << "Error removing log file " << leafNameFullPath.toPath(*fsAccess) << std::endl;
                    }
                }
            }
        });
    }

    LocalPath logArchive_getNewFilename(LocalPath fileName)
    {
        return archiveType == archiveTypeNumbered
                ? logArchiveNumbered_getFilename(fileName, 0)
                : logArchiveTimestamp_getFilename(fileName);
    }

    void logArchive_cleanUpFiles(LocalPath logsPath, LocalPath fileName)
    {
        if (archiveType == archiveTypeNumbered)
        {
            logArchiveNumbered_cleanUpFiles(logsPath, fileName);
        }
        else
        {
            logArchiveTimestamp_cleanUpFiles(logsPath, fileName);
        }
    }

    void logArchive_rotateFiles(LocalPath logsPath, LocalPath fileName)
    {
        if (archiveType == archiveTypeNumbered)
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
        std::error_code ec;

        while (!logExit)
        {
            if (forceRenew)
            {
                std::lock_guard<std::mutex> g(logRotationMutex);
                logArchive_cleanUpFiles(logsPath, fileName);

                outputFile.close();


                if (!fsAccess->unlinklocal(fileNameFullPath))
                {
                    std::cerr << "Error removing log file " << fileNameFullPath.toPath(*fsAccess) << std::endl;
                }

                outputFile.open(fileNameFullPath.localpath.c_str(), std::ofstream::out);

                outFileSize = 0;

                forceRenew = false;
            }
            else if (outFileSize > MAX_FILESIZE_MB*1024*1024)
            {
                std::lock_guard<std::mutex> g(logRotationMutex);
                logArchive_rotateFiles(logsPath, fileName);

                auto newNameDone = logsPath;
                newNameDone.appendWithSeparator(logArchive_getNewFilename(fileName), false);
                auto newNameZipping = newNameDone;
                newNameZipping.append(LocalPath::fromPlatformEncoded(".zipping"));

                outputFile.close();
                fsAccess->unlinklocal(newNameZipping);
                fsAccess->renamelocal(fileNameFullPath, newNameZipping, true);

                std::thread t([=]() {
                    std::lock_guard<std::mutex> g(logRotationMutex); // prevent another rotation while we work on this file (in case of unfortunate timing with bug report etc)
                    gzipCompressOnRotate(newNameZipping, newNameDone);
                });
                t.detach();

                outputFile.open(fileNameFullPath.localpath.c_str(), std::ofstream::out);
                outFileSize = 0;
            }

            LogLinkedList* newMessages = nullptr;
            bool topLevelMemoryGap = false;
            {
                std::unique_lock<std::mutex> lock(logMutex);
                logConditionVariable.wait_for(lock, std::chrono::milliseconds(500), [this, &newMessages, &topLevelMemoryGap]() {
                        if (forceRenew || logListFirst.next || logExit || flushLog || closeLog)
                        {
                            newMessages = logListFirst.next;
                            logListFirst.next = nullptr;
                            logListLast = &logListFirst;
                            topLevelMemoryGap = logListFirst.oomGap;
                            logListFirst.oomGap = false;
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
                newMessages = newMessages->next;
                if (outputFile)
                {
                    if (p->needsDirectOutput())
                    {
                        (*p->mDirectLoggingFunction)(&outputFile);
                    }
                    else
                    {
                        outputFile << p->message;
                        outFileSize += p->used;
                        if (p->oomGap)
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
                        std::cout << p->message;
                    }
                    std::cout << std::flush; //always flush into stdout (DEBUG mode)
                }
                p->notifyWaiter();
                free(p);
            }
            if (flushLog || nextFlushTime <= std::chrono::steady_clock::now())
            {
                flushLog = false;
                outputFile.flush();
                if (RotativePerformanceLogger::Instance().mLogToStdout)
                {
                    std::cout << std::flush;
                }
                nextFlushTime = std::chrono::steady_clock::now() + logFlushPeriod;
            }

            if (closeLog)
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
        std::lock_guard<std::mutex> g(g_loggingThread->logMutex);
        g_loggingThread->logExit = true;
        g_loggingThread->logConditionVariable.notify_one();
    }
    g_loggingThread->logThread->join();
    g_loggingThread->logThread.reset();
}

void RotativePerformanceLogger::initialize(const char * logsPath, const char * logFileName, bool logToStdout)
{
    auto logsPathLocalPath = LocalPath::fromPlatformEncoded(logsPath);
    auto logFileNameLocalPath = LocalPath::fromPlatformEncoded(logFileName);

    mLogToStdout = logToStdout;

    MegaFileSystemAccess *fsAccess = new MegaFileSystemAccess();
    fsAccess->mkdirlocal(logsPathLocalPath, false);

    g_loggingThread.reset(new RotativePerformanceLoggerLoggingThread());
    g_loggingThread->startLoggingThread(logsPathLocalPath, logFileNameLocalPath);

    MegaApi::setLogLevel(MegaApi::LOG_LEVEL_MAX);
    MegaApi::addLoggerObject(this);
}

RotativePerformanceLogger& RotativePerformanceLogger::Instance() {
    static RotativePerformanceLogger myInstance;
    return myInstance;
}

void RotativePerformanceLogger::setArchiveNumbered()
{
    g_loggingThread->archiveType = archiveTypeNumbered;
}

void RotativePerformanceLogger::setArchiveTimestamps(long int maxFileAgeSeconds)
{
    g_loggingThread->archiveType = archiveTypeTimestamp;
    g_loggingThread->archiveMaxFileAgeSeconds = maxFileAgeSeconds;
}

inline void twodigit(char*& s, int n)
{
    *s++ = (char) (n / 10 + '0');
    *s++ = (char) (n % 10 + '0');
}

char* filltime(char* s, struct tm*  gmt, int microsec)
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
    s[5] = (char) (microsec % 10 + '0');
    s[4] = (char) ((microsec /= 10) % 10 + '0');
    s[3] = (char) ((microsec /= 10) % 10 + '0');
    s[2] = (char) ((microsec /= 10) % 10 + '0');
    s[1] = (char) ((microsec /= 10) % 10 + '0');
    s[0] = (char) ((microsec /= 10) % 10 + '0');
    s += 6;
    *s++ = ' ';
    *s = 0;
    return s;
}

std::mutex threadNameMutex;
std::map<std::thread::id, std::string> threadNames;
struct tm lastTm;
time_t lastT = 0;
std::thread::id lastThreadId;
const char* lastThreadName;

void cacheThreadNameAndTimeT(time_t t, struct tm& gmt, const char*& threadname)
{
    std::lock_guard<std::mutex> g(threadNameMutex);

    if (t != lastT)
    {
        lastTm = *std::gmtime(&t);
        lastT = t;
    }
    gmt = lastTm;

    if (lastThreadId == std::this_thread::get_id())
    {
        threadname = lastThreadName;
        return;
    }

    auto& entry = threadNames[std::this_thread::get_id()];
    if (entry.empty())
    {
        std::ostringstream s;
        s << std::this_thread::get_id() << " ";
        entry = s.str();
    }
    threadname = lastThreadName = entry.c_str();
    lastThreadId = std::this_thread::get_id();
}

void RotativePerformanceLogger::log(const char*, int loglevel, const char*, const char *message
#ifdef ENABLE_LOG_PERFORMANCE
                         , const char **directMessages, size_t *directMessagesSizes, int numberMessages
#endif
                         )

{
    g_loggingThread->log(loglevel, message
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
    cacheThreadNameAndTimeT(t, gmt, threadname);

    auto microsec = std::chrono::duration_cast<std::chrono::microseconds>(now - std::chrono::system_clock::from_time_t(t));
    filltime(timebuf, &gmt, (int)microsec.count() % 1000000);

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
        std::unique_ptr<std::lock_guard<std::mutex>> g(new std::lock_guard<std::mutex>(logMutex));

        bool isRepeat = !direct && logListLast != &logListFirst &&
                        logListLast->lastmessage >= 0 &&
                        !strncmp(message, logListLast->message + logListLast->lastmessage, messageLen);

        if (isRepeat)
        {
            ++logListLast->lastmessageRepeats;
        }
        else
        {
            unsigned reportRepeats = logListLast != &logListFirst ? logListLast->lastmessageRepeats : 0;
            if (reportRepeats)
            {
                lineLen += 30;
                logListLast->lastmessageRepeats = 0;
            }

            if (direct)
            {
                if (LogLinkedList* newentry = LogLinkedList::create(logListLast, 1 + sizeof(LogLinkedList))) //create a new "empty" element
                {
                    logListLast = newentry;
                    std::promise<void> promise;
                    logListLast->mCompletionPromise = &promise;
                    auto future = logListLast->mCompletionPromise->get_future();
                    DirectLogFunction func = [&timebuf, &threadname, &loglevelstring, &directMessages, &directMessagesSizes, numberMessages](std::ostream *oss)
                    {
                        *oss << timebuf << threadname << loglevelstring;

                        for(int i = 0; i < numberMessages; i++)
                        {
                            oss->write(directMessages[i], directMessagesSizes[i]);
                        }
                        *oss << std::endl;
                    };

                    logListLast->mDirectLoggingFunction = &func;

                    g.reset(); //to liberate the mutex and let the logging thread call the logging function

                    logConditionVariable.notify_one();

                    //wait for until logging thread completes the outputting
                    future.get();
                    return;
                }
                else
                {
                    logListLast->oomGap = true;
                }

            }
            else
            {
                if (logListLast == &logListFirst || logListLast->oomGap || !logListLast->messageFits(lineLen))
                {
                    if (LogLinkedList* newentry = LogLinkedList::create(logListLast, std::max<size_t>(lineLen, 8192) + sizeof(LogLinkedList) + 10))
                    {
                        logListLast = newentry;
                    }
                    else
                    {
                        logListLast->oomGap = true;
                    }
                }
                if (!logListLast->oomGap)
                {
                    if (reportRepeats)
                    {
                        char repeatbuf[31]; // this one can occur very frequently with many in a row: cURL DEBUG: schannel: failed to decrypt data, need more data
                        int n = snprintf(repeatbuf, 30, "[repeated x%u]\n", reportRepeats);
                        logListLast->append(repeatbuf, n);
                    }
                    logListLast->append(timebuf, LOG_TIME_CHARS);
                    logListLast->append(threadname, unsigned(threadnameLen));
                    logListLast->append(loglevelstring, LOG_LEVEL_CHARS);
                    logListLast->lastmessage = logListLast->used;
                    logListLast->append(message, unsigned(messageLen));
                    logListLast->append("\n", 1);
                    notify = logListLast->used + 1024 > logListLast->allocated;
                }
            }
        }

        if (loglevel <= flushOnLevel)
        {
            flushLog = true;
        }
    }

    if (notify)
    {
        // notify outside the mutex lock is better (and correct) for much less chance the other
        // thread wakes up just to find the mutex locked. (saw lower cpu on the other thread like this)
        // Still, this notify call was taking 1% when notifying on every log line, so let the other thead
        // wake up by itself every 500ms without notify for the common case.
        // But still wake it if our memory block is getting full
        logConditionVariable.notify_one();
    }
}

bool RotativePerformanceLogger::cleanLogs()
{
    std::lock_guard<std::mutex> g(g_loggingThread->logMutex);
    g_loggingThread->forceRenew = true;
    g_loggingThread->logConditionVariable.notify_one();
    return true;
}

void RotativePerformanceLogger::flushAndClose()
{
    try
    {
        g_loggingThread->log(MegaApi::LOG_LEVEL_FATAL, "***CRASH DETECTED: FLUSHING AND CLOSING***");

    }
    catch (const std::exception& e)
    {
        std::cerr << "Unhandle exception on flushAndClose: "<< e.what() << std::endl;
    }
    g_loggingThread->flushLog = true;
    g_loggingThread->closeLog = true;
    g_loggingThread->logConditionVariable.notify_one();
    // This is called on crash so the app may be unstable. Don't assume the thread is working properly.
    // It might be the one that crashed.  Just give it 1 second to complete
#ifdef WIN32
    Sleep(1000);
#else
    usleep(1000000);
#endif
}

}
