/**
 * @file mega/processs.h
 * @brief Mega SDK sun sub process
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

#ifndef MEGA_PROCESS_H
#define MEGA_PROCESS_H 1

#include "mega/utils.h"

#ifdef WIN32
#include <process.h>
#else
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include <assert.h>
#include <string>
#include <vector>
#include <unordered_set>
#include <unordered_map>
#include <set>

namespace mega
{


/**
 * @brief The Process class
 *
 * It allows to create child processes with its stdout, stderr (stdin is optional) redirected to
 * this object through a function of type DataReaderFunc / void(unsigned char *, size_t len).
 *
 * It also provides a buffer for writing to the child process's stdin.
 *
 * Intearnally, it uses execvp() instead of popen() and system(), as the latter have the following deficiencies:
 *    - they can not be cross platform as the command string is interpreted bny the shell
 *      a string variable argument can break a program, e.g. a " in an input string.
 *      Process takes a vector<string> of args
 *    - the exit code or signal that killed the process is reported
 *    - reads stdoutand stderr separately
 *      flexible handling of stdout and stderr bytes via a function<void(const unsigned char* data, size_t len)>
 *
 * Additionally, execvp() provides the following features:
 *    - can write to stdin
 *    - can set env vars
 *    - can set startup directory
 *
 *  How to use (example):
 *    Process::StringSink out;
 *    Process::StringSink error;
 *    Process p;
 *    p.run({ "ls", "-l" }, "", {}, out.func(), error.func());
 *    if (p.wait()) return out; // completed ok
 *    LOG_err << "command failed: " << p.getExitMessage() << ": " << error;
 */
class Process
{
public:
    typedef std::function<void(const unsigned char* data, size_t len)> DataReaderFunc;

private:
    class AutoFileHandle
    {

    #ifdef WIN32
        using HandleType = HANDLE;
        const HandleType UNSET = INVALID_HANDLE_VALUE;
    #else // _WIN32
        using HandleType = int;
        const HandleType UNSET = -1;
    #endif // ! _WIN32

        HandleType h = UNSET;

    public:
        AutoFileHandle() {}
        AutoFileHandle(HandleType ih) : h(ih) {}

        ~AutoFileHandle()
        {
            close();
        }

        void close()
        {
            if (h != UNSET)
            {
    #ifdef WIN32
                ::CloseHandle(h);
    #else
                ::close(h);
    #endif
            }

            h = UNSET;
        }

        AutoFileHandle& operator=(HandleType ih)
        {
            // avoid to leak a handle if changed
            if (ih != h) close();

            h = ih;
            return *this;
        }

        bool isSet() const { return h != UNSET; }

        // implicit conversion, so can pass into OS API
        operator HandleType() const { return h; }
        HandleType* ptr() { return &h; }
        HandleType get() const { return h; }
    };

#ifdef WIN32
    DWORD childPid = (DWORD)-1;
#else
    // all values valid and may be up to a long
    pid_t childPid = -1;
#endif

    bool launched = false;

    // reads stdout from the sub process
    AutoFileHandle readFd;
    // reads stderr from the sub process
    AutoFileHandle readErrorFd;

#ifdef WIN32
    DWORD writePipeLength = 0xFFFFFFFF;
#endif

    // may be nullptr
    // if nullptr child stdout will be echoed to stdout
    DataReaderFunc stdoutReader = nullptr;

    // may be nullptr
    // if nullptr child stderr will be echoed to stderr
    DataReaderFunc stderrReader = nullptr;

    // undefined if !hasExited && !hasBeenSignalled
    // set in CheckExit() or Wait()
    // Unix: status set by waitpid()
    // Windows: exit code, may be 0xFFFFFFFF so we need a separate flag
    // encaptuated as should check with OS each time
    int status = -1;

    // 'status' contains an exit code from the subprocess
    bool hasExitStatus = false;

    // 'status' contains the signal that terminated the subvproceess
    bool hasSignalStatus = false;

public:
    bool hasStatus() const
    {
        return hasExitStatus || hasSignalStatus;
    }

    // unsigned char as these are bytes not text
    //
    // a string that is adapted to be populated by Process
    class StringSink : public std::string
    {
    public:
        using std::string::operator=;
        DataReaderFunc func() {
            return [this](const unsigned char* data, size_t len) {
                append(reinterpret_cast<const char*>(data), len);
            };
        }
    };

    bool hasExited() const
    {
        return hasExitStatus;
    }
    bool hasTerminateBySignal() const
    {
        return hasSignalStatus;
    }

    // need to flush() after exited
    bool hasExitedOk() const
    {
        return hasExitStatus && getExitCode() == 0;
    }

    // 0..255 on Unix, or -1 on internal error
    // DWORD on Windows
    int getExitCode() const
    {
        assert(hasExitStatus);
        return status;
    }

    int getTerminatingSignal() const
    {
        assert(hasSignalStatus);
        return status;
    }

    Process() {}
    ~Process();

    /**
     * @brief run TODO
     *
     * @param args arguments, PATH is searched for argv[0]
     * @param directory Startup direcotry, "", the default for current directory
     * @param env Set enironemnt variables, value of "" to remove an environemnt variable
     * @param ireader To handle child stdout, defaults to writing to parent stdout
     * @param istderrReader To handle child stderr defaults to writing to parent stderr
     * @param iredirectStdin Allow write()ing to stdin of sub process
     *
     * @return TODO
     */
    bool run(const std::vector<std::string>& args,
             const std::unordered_map<std::string, std::string>& env = std::unordered_map<std::string, std::string>(),
             DataReaderFunc ireader = nullptr,
             DataReaderFunc istderrReader = nullptr);

#ifdef WIN32
    static std::string windowsQuoteArg(const std::string& str);
#endif
    // produce string of command line
    // passed to OS on Windows, just used for trace on posix
    static std::string formCommandLine(const std::vector<std::string>& args);

    // return true if read some
    bool poll();

    // poll() until nothing is read nor written
    // return true if anything read or written
    bool flush();

    // close the pipes but leave the process alive
    void close();

    bool isOpen() const { return isStdOutOpen() || isStdErrOpen(); }
    bool isStdOutOpen() const { return readFd.isSet(); }
    bool isStdErrOpen() const { return readErrorFd.isSet(); }

    // return false if already terminated
    bool terminate();

    // return true if child has status (exiited)
    // e.g. exited or signalled
    // fill in 'status' if get signal or exits
    bool checkStatus();

    // wait for child to exit
    // returns true if exited ok
    // call hasExited()/getExitCode() or hasExitedBySignal() to work out why has terminated
    bool wait();

    // return true if the processs is still running
    // a dead process may still have buffered output available in poll()
    // use hasExited() or hasTerminateBySignal() to determine the exit type
    // need to flush() after exited
    bool isAlive() { return !checkStatus(); }
        
    // Unix only
    // returns description
    std::string getExitSignalDescription() const;

    // return "SIGTERM - Termination Signal"
    static std::string describeSignal(int sig);

    // return description of exit
    // "Exited ok"
    // "Exited with status 3"
    // "Exited with signal: SIGTERM - Termination Signal"
    std::string getExitMessage() const;

    // Return the current process ID
    // Value returned is valid only if the process has been run
    int getPid() const { return childPid; }

private:    // internal methods

    void clearStatus()
    {
        status = -1;
        hasExitStatus = false;
        hasSignalStatus = false;
    }

    void setExitStatus(int istatus)
    {
        status = istatus;
        hasExitStatus = true;
        hasSignalStatus = false;
    }

    void setSignalledStatus(int istatus)
    {
        status = istatus;
        hasExitStatus = false;
        hasSignalStatus = true;
    }

    // called when we can not ascertain the shild process's status
    void setWaitFailureStatus()
    {
        // otherwise may spin forever
        setExitStatus(-1);
    }

    void setLaunchFailureStatus()
    {
        // otherwise may spin forever
        setExitStatus(-1);
    }

    // return true if read some
    bool readStdout();

    // return true if read some
    bool readStderr();

    // used to temporarily set environment variables
    // on Windows the API requires the entire environment to be specified
    // one implementation on both platforms
    class EnvironmentChanger
    {
        std::unordered_map<std::string, std::string> saved;
        std::unordered_set<std::string> unset;

    public:
        EnvironmentChanger(const std::unordered_map<std::string, std::string>& env);
        // update environemnt
        // save variabled being overwritten
        ~EnvironmentChanger();
    };
};

// print a progress value, bar and Estimated Time To Arrival
class ConsoleProgressBar
{
    size_t value = 0;
    size_t max = 0;

    ::mega::m_time_t start = 0;
    size_t barWidth = 40;
    std::string prefix;

    // write a new line after printing
    bool mWriteNewLine = false;

    // automatically print to cout when updated
    bool autoOutput = true;

public:

    ConsoleProgressBar(size_t imax, bool writeNewLine);

    void add(size_t n);

    void inc();

    std::ostream& put(std::ostream& os) const;

    // display in stdout
    void show() const;
    void setPrefix(const std::string &value);
};

// must be outside mega namespace
inline std::ostream& operator<<(std::ostream& os, const ConsoleProgressBar& bar)
{
    return bar.put(os);
}

} // namespace mega

#endif
