/**
 * @file process.cpp
 * @brief Mega SDK run subprocess
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

#include "mega/process.h"

#include "mega.h"

#ifdef WIN32
#else
#include <unistd.h>
#include <fcntl.h>
#endif

#include <algorithm>

using namespace mega;
using namespace std;

Process::EnvironmentChanger::EnvironmentChanger(const unordered_map<string, string>& env)
{
    for (auto& i : env)
    {
        if (const auto [val, hasValue] = Utils::getenv(i.first); hasValue)
        {
            saved[i.first] = val;
        }
        else 
        {
            unset.insert(i.first);
        }
        Utils::setenv(i.first, i.second);
    }
}

Process::EnvironmentChanger::~EnvironmentChanger()
{
    for (auto& i : saved)
    {
        Utils::setenv(i.first, i.second);
    }
    for (auto& i : unset)
    {
        Utils::unsetenv(i);
    }
}

bool Process::run(const vector<string>& args, const unordered_map<string, string>& env,
                  DataReaderFunc istdoutReader, DataReaderFunc istderrReader)
{
    LOG_debug << "Process::Process(\"" << formCommandLine(args) << "...)";

    stdoutReader = istdoutReader;
    stderrReader = istderrReader;

#ifdef WIN32
    string cmdLine = formCommandLine(args);
    LOG_debug << "cmdLine = '" << cmdLine << "'";

    EnvironmentChanger envChanger(env);

    AutoFileHandle stdoutHandleChild;
    AutoFileHandle stderrHandleChild;

    SECURITY_ATTRIBUTES saAttr;
    // Set the bInheritHandle flag so pipe handles are inherited. 
    saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
    saAttr.bInheritHandle = TRUE;
    saAttr.lpSecurityDescriptor = nullptr;

    // Connect streams from child process to streams declared in parent process
    // stdout
    if (!CreatePipe(readFd.ptr(), stdoutHandleChild.ptr(), &saAttr, 0))
    {
        reportWindowsError("Could not create pipe for child stdout");
        return false;
    }        
    DWORD pipeMode = PIPE_NOWAIT | PIPE_READMODE_BYTE;
    if (!SetNamedPipeHandleState(readFd, &pipeMode, nullptr, nullptr)) 
    {
        reportWindowsError("Could not make readFd non blocking");
        return false;
    }
    // stderr
    if (!CreatePipe(readErrorFd.ptr(), stderrHandleChild.ptr(), &saAttr, 0))
    {
        reportWindowsError("Could not create pipe for child stderror");
        return false;
    }
    pipeMode = PIPE_NOWAIT | PIPE_READMODE_BYTE;
    if (!SetNamedPipeHandleState(readErrorFd, &pipeMode, nullptr, nullptr))
    {
        reportWindowsError("Could not make readErrorFd  non blocking");
        return false;
    }

    PROCESS_INFORMATION piProcInfo;

    // Set up members of the PROCESS_INFORMATION structure. 
    ZeroMemory(&piProcInfo, sizeof(PROCESS_INFORMATION));

    // Set up members of the STARTUPINFO structure. 
    STARTUPINFO siStartInfo;
    ZeroMemory(&siStartInfo, sizeof(STARTUPINFO));
    siStartInfo.cb = sizeof(STARTUPINFO);
    siStartInfo.hStdError = stderrHandleChild;
    siStartInfo.hStdOutput = stdoutHandleChild;
    siStartInfo.hStdInput = INVALID_HANDLE_VALUE;
    siStartInfo.dwFlags |= STARTF_USESTDHANDLES;

    wstring cmdLineW;
    LocalPath::path2local(&cmdLine, &cmdLineW);

    BOOL r = CreateProcess(
        nullptr,                // appName - use 1st param from commandLine
        (LPWSTR)cmdLineW.c_str(),       // command line
        nullptr,                // process security attributes
        nullptr,                // primary thread security attributes
        TRUE,                   // handles are inherited
        DETACHED_PROCESS,       // creation flags
        nullptr,                // use parent's environment
        nullptr,                // use parent's current directory (nullptr) or named dir
        &siStartInfo,           // STARTUPINFO pointer
        &piProcInfo);           // receives PROCESS_INFORMATION

    if (!r)
    {
        reportWindowsError("Could not spawn child process '" + cmdLine + "'");
        return false;
    }

    launched = true;
    ::CloseHandle(piProcInfo.hThread);
    ::CloseHandle(piProcInfo.hProcess);
        
    childPid = piProcInfo.dwProcessId;

    return true;

#else   // WIN32

    // Connect streams from child process to streams declared in parent process
    // Note: read from [0], write to [1]
    // stdout
    int parentReadFd[2] = { -1, -1 };
    if (pipe(parentReadFd) != 0)
    {
        reportError("Could not open pipe()");
        return false;
    }
    readFd = parentReadFd[0];
    int childReadFd = parentReadFd[1];
    // stderr
    int parentReadErrorFd[2] = { -1, -1 };
    if (pipe(parentReadErrorFd) != 0)
    {
        reportError("Could not open second pipe()");
        return false;
    }
    readErrorFd = parentReadErrorFd[0];
    int childReadErrorFd = parentReadErrorFd[1];
        
    EnvironmentChanger envChanger(env);

    // Create child process via fork()
    LOG_debug << "fork()";    
    childPid = ::fork();
    if (childPid == -1)
    {
        ::close(childReadFd);
        ::close(childReadErrorFd);
        reportError("Could not fork()");
        return false;
    }

    launched = true;

    // Child process - specific code
    if (childPid == 0)
    {
        // These belong to the parent now.
        ::close(readFd);
        ::close(readErrorFd);

        // Redirect streams to the parent process
        // stdout
        ::close(STDOUT_FILENO);
        if (dup2(childReadFd, STDOUT_FILENO) == -1)
        {
            reportError("Could not redirect stdout");
            exit(1);
        }
        // stderr
        ::close(STDERR_FILENO);
        if (dup2(childReadErrorFd, STDERR_FILENO) == -1)
        {
            reportError("Could not redirect stderr");
            exit(1);
        }
        
        // Prepare command-line arguments for child process
        if (args.empty())
        {
            cerr << "Process: Can not execute, no arguments given" << endl;
            exit(1);
        }
        vector<char*> argv;
        for (vector<string>::const_iterator i = args.begin(); i != args.end(); ++i)
        {
            argv.push_back(strdup(i->c_str()));
        }
        argv.push_back(nullptr);

        // Execute 1st param (our executable)
        execvp(argv[0], &argv.front());

        // --- Only if execvp() fails, the code below this point is executed ---
        // (log error and exit)
        int savedErrno = errno;
        // cerr so parent process sees this
        cerr << "Could not execute '" + string(argv[0]) + "'" << ": " << savedErrno << ": " << strerror(savedErrno) << endl;
        reportError("Could not execute '" + string(argv[0]) + "'", savedErrno);
        exit(1);
    }
//    else --> parent process

    // These belong to the child now.
    ::close(childReadFd);
    ::close(childReadErrorFd);

    // Set the read/write from/to child process streams as non-blocking
    // stdout of child process
    if (fcntl(readFd, F_SETFL, fcntl(readFd, F_GETFL) | O_NONBLOCK) == -1)
    {
        reportError("Could not make readFd non blocking");
        return false;
    }
    // stderr of child process
    if (fcntl(readErrorFd, F_SETFL, fcntl(readErrorFd, F_GETFL) | O_NONBLOCK) == -1)
    {
        reportError("Could not make readErrorFd non blocking");
        return false;
    }

    return true;
#endif // else WIN32
}

bool Process::terminate()
{
    // return false if already terminated
    LOG_debug << "Process::terminate()";

    if (hasStatus())
    {
        LOG_debug << "already exited";
        return false;
    }

#ifdef WIN32
    AutoFileHandle childHandle = ::OpenProcess(PROCESS_TERMINATE, false, childPid);
    if (childHandle == nullptr)
    {
        DWORD error = ::GetLastError();
        if (error == ERROR_INVALID_PARAMETER)
        {
            // no such PID
            return false;
        }
        reportWindowsError("Failed to OpenProcess() for spawned process PID " + to_string(childPid));
        return false;
    }
    LOG_debug << "calling TerminateProcess()";
    if (::TerminateProcess(childHandle, 0))
    {
        // 0 = exit code
        // returns immediately

        LOG_debug << "Termination started";
    }
    else
    {
        // TerminateProcess() failed
        reportWindowsError("Failed to TerminateProcess() for spawned process PID " + to_string(childPid));
        return false;
    }
    return true;

#else
    // !WIN32
    if (!isAlive())
    {
        LOG_debug << "not alive";
        return false;
    }
    LOG_debug << "kill(" << childPid << ", SIGTERM)";
    if (kill(childPid, SIGTERM) != 0)
    {
        reportError("Error terminating child process " + to_string(childPid));
        return false;
    }
    return true;
#endif
}

Process::~Process()
{
    LOG_debug << "Process::~Process()";
    close();
    terminate();
}

void Process::close()
{
    LOG_debug << "Process::close()";
    readFd.close();
    readErrorFd.close();
}

bool Process::poll()
{
    bool ro = readStdout();
    //LOG_debug << "readStdout() -> " << ro;
    if (!isOpen())
    {
        LOG_debug << " Process::poll(): closed after read stdout";
        return ro;
    }

    bool re = readStderr();
    //LOG_debug << "readStderr() -> " << re;
    if (!isOpen())
    {
        LOG_debug << " Process::poll(): closed after read stderr";
    }

    return ro || re;
}

bool Process::readStdout()
{

    //LOG_debug << "readStdout()";
    if (!isStdOutOpen())
    {       
        //LOG_debug << "isStdOutOpen() false";
        return false;
    }

    unsigned char buf[32 * 1024];

#ifdef WIN32
    DWORD len;
    if (!ReadFile(readFd, buf, sizeof buf, &len, nullptr))
    {
        int savedError = GetLastError();
        if (savedError == ERROR_NO_DATA)
            return false;
        reportWindowsError("Process::readStdout() error", savedError);
        close();
        return false;
    }
    else if (len == 0)
    {
        return false;
    }
    else
    {
        //LOG_debug << "Process::readStdout() read " << len;
        if (stdoutReader != nullptr)
        {
            stdoutReader(buf, (size_t)len);
        }
        else
        {
            fwrite(buf, 1, len, stdout);
        }
        return true;
    }
#else
    ssize_t len = read(readFd, buf, sizeof buf);
    if (len == 0)
    {
        // when child exits get continuious reads of 0 bytes
        //LOG_debug << "read() " << len;
        return false;
    }
    else if (len == -1)
    {
        if (errno == EAGAIN)
        {
            // no data available
            // when child alive but not writing get alternating EAGAIN and reads of 0 bytes
            //LOG_debug << "EAGAIN";
            return false;
        }
        //LOG_debug << "read() " << len;
        reportError("Process::readStdout() error");
        close();
        return false;
    }
    else
    {
        //LOG_debug << "Process::readStdout() read " << len;
        if (stdoutReader != nullptr)
        {
            stdoutReader(buf, (size_t)len);
        }
        else if (::write(STDOUT_FILENO, buf, (size_t)len) < 0)
        {
            reportError("Process::readStdout() -> ::write() error: " + std::to_string(errno));
        }
        return true;
    }
#endif
}

bool Process::readStderr()
{
    if (!isStdErrOpen()) return false;

    unsigned char buf[32 * 1024];
#ifdef WIN32
    DWORD len;
    if (!ReadFile(readErrorFd, buf, sizeof buf, &len, nullptr))
    {
        int savedError = GetLastError();
        if (savedError == ERROR_NO_DATA) return false;
        reportWindowsError("Process::readStderror() error", savedError);
        close(); 
        return false;
    }
    else if (len == 0)
    {
        return false;
    }
    else {
        //LOG_debug << "Process::readStderr() read " << len;
        if (stderrReader != nullptr)
        {
            stderrReader(buf, (size_t)len);
        }
        else
        {
            fwrite(buf, 1, len, stderr);
        }
        return true;
    }
#else
    ssize_t len = read(readErrorFd, buf, sizeof(buf));
    if (len == 0)
    {
        // when child exits get continuious reads of 0 bytes
        return false;
    }
    else if (len == -1)
    {
        if (errno == EAGAIN)
        {
            // no data available
            return false;
        }
        reportError("Process::readStderr() error");
        close();
        return false;
    }
    else
    {
        //LOG_debug << "Process::readStderr() read " << len;
        if (stderrReader != nullptr)
        {
            stderrReader(buf, (size_t)len);
        }
        else if (::write(STDOUT_FILENO, buf, (size_t)len) < 0)
        {
            reportError("Process::readStderr() -> ::write() error: " + std::to_string(errno));
        }
        return true;
    }
#endif
}

std::string Process::getExitSignalDescription() const
{
    // Unix only
    // returns SIG* description
    return describeSignal(getTerminatingSignal());
}

//static 
string Process::describeSignal(int sig)
{
#ifdef WIN32
    assert(!"Process::DescribeSignal not implemented on Windows");
    return "[Process::DescribeSignal(" + to_string(sig) + ") called on Windows]";
#else
    const char* str = strsignal(sig);
    return str ? string(str) : "[Unknown signal #" + to_string(sig) + "]";
#endif
}

string Process::getExitMessage() const
{
    // return description of exit
    // "Exited ok"
    // "Exited with status 3"
    // "Terminated with signal: SIGTERM - Termination Signal"
    if (hasExited())
    {
        int rc = getExitCode();
        if (rc == 0)
            return "Exited ok";
        else
            return "Exited with status " + to_string(rc);
    }
    else if (hasTerminateBySignal())
    {
        return "Terminated with signal: " + describeSignal(getTerminatingSignal());
    }
    else
    {
        return "Unknown process status " + to_string(status);
    }
}

bool Process::checkStatus()
{
    // return true if child has reported status (exited)
        
    if (hasStatus())
    {
        //LOG_debug << "already hasStatus " << status;
        return true;
    }

    if (!launched)
    {
        //LOG_debug << "not lanuched";
        setLaunchFailureStatus(); // otherwise may spin forever
        return true;
    }
#ifdef WIN32
    // If an app exits with code 259 will look like they are still running
    // so open the process using the PID every time
    // 259 may be a reference to the face that on Unix if a process exits with 256
    // the wait()er [parent] sees 0! Try "exit 256" is in sh script, $? is 0.
    AutoFileHandle tmpChildHandle = ::OpenProcess(PROCESS_QUERY_INFORMATION, false, childPid);
    if (tmpChildHandle == nullptr)
    {
        DWORD error = ::GetLastError();
        if (error != ERROR_INVALID_PARAMETER)
        {
            // no such PID              
            reportWindowsError("Could not OpenProcess() for spwaned process PID " + to_string(childPid));
        }
        setWaitFailureStatus(); // otherwise may spin forever
        return true;
    }
    DWORD exitCode = 0;
    if (!::GetExitCodeProcess(tmpChildHandle, &exitCode))
    {
        reportWindowsError("Could not GetExitCodeProcess() for spwaned process");
        setWaitFailureStatus(); // otherwise may spin forever
        return true;
    }

    if (exitCode != STILL_ACTIVE)
    {
        setExitStatus(exitCode);
    }

    return hasStatus();
#else
    // !WIN32
    int tmp = 0;
    int rc = waitpid(childPid, &tmp, WNOHANG);
    if (rc < 0)
    {
        reportError("Can not wait on child PID " + to_string(childPid));
        setWaitFailureStatus(); // otherwise may spin forever
        return true;
    }
    if (rc == 0)
    {
        //LOG_debug << "still running";
        return false;
    }
    if (rc == childPid)
    {
        //LOG_debug << "childPid" << childPid;
        if (WIFEXITED(tmp))
        {
            setExitStatus(WEXITSTATUS(tmp));
        }
        else if (WIFSIGNALED(tmp))
        {
            setSignalledStatus(WTERMSIG(tmp));
        }
        else
        {
            assert(!"waitpid() but not exited not signalled");
            setWaitFailureStatus(); // otherwise may spin forever
        }
        return true;
    }
    reportError("Error waiting on child PID " + to_string(childPid) + " returned status for PID " + to_string(rc));
    setWaitFailureStatus(); // otherwise may spin forever
    return true;
#endif
}

bool Process::wait()
{
    // can not wait() forever on Posix as need to read stdout and stderr
    if (hasStatus())  return hasExitedOk();

    while (!checkStatus())
    {
        if (!poll())
            usleep(100000); // 0.1 sec
    }
    // can have buffered output after exited
    flush();

    return hasExitedOk();
}

bool Process::flush()
{
    bool action = false;
    while (poll())
    {
        action = true;
    }
    return action;
}

#ifdef WIN32
//static
string Process::windowsQuoteArg(const string& str)
{
    if (str.empty() || str.find_first_of(" \t\\\"") != string::npos)
    {
        // "quote spaces"

        // must be compatible with Win32 CommandLineToArgvW() 
        //   https://msdn.microsoft.com/en-us/library/windows/desktop/bb776391(v=vs.85).aspx
        // 1. 2n backslashes followed by a quotation mark produce n backslashes followed by a quotation mark.
        // 2. (2n) +1 backslashes followed by a quotation mark again produce n backslashes followed by a quotation mark.
        // 3. n backslashes not followed by a quotation mark simply produce n backslashes.

        string buffer = "\"";
        int numBackslashes = 0;
        for(char chr: str)
        {
            if (chr == '\"')
            {
                // (2n) +1 backslashes followed by a quotation mark again produce n backslashes followed by a quotation mark.
                for (int i = 0; i < numBackslashes; ++i)
                    buffer += '\\';
                buffer += '\\';
                numBackslashes = 0;
            }
            else if (chr == '\\')
            {
                ++numBackslashes;
            }
            else
            {
                numBackslashes = 0;
            }
            buffer += chr;
        }
        if (numBackslashes > 0)
        {
            // 2n backslashes followed by a quotation mark produce n backslashes followed by a quotation mark.
            for (int i = 0; i < numBackslashes; ++i)
                buffer += '\\';
        }
        buffer += '"';
        return buffer;
    }
    return str;
}

//static 
string Process::formCommandLine(const std::vector<std::string>& args)
{
    string cmdLine;
    bool first = true;
    for (vector<string>::const_iterator i = args.begin(); i != args.end(); ++i)
    {
        if (!first)
            cmdLine += " ";
        cmdLine += windowsQuoteArg(*i);
        first = false;
    }
    return cmdLine;
}

#else
//static
// for display only
string Process::formCommandLine(const std::vector<std::string>& args)
{
    return Utils::join(args, " ");
}
#endif

void ConsoleProgressBar::show() const
{
    std::cout << '\r';
    put(std::cout);
    if (mWriteNewLine)
        std::cout << std::endl;
    else
        std::cout << '\r';
}

void ConsoleProgressBar::setPrefix(const std::string &value)
{
    prefix = value;
}

ConsoleProgressBar::ConsoleProgressBar(size_t imax, bool writeNewLine)
    : max(imax), start(::mega::m_time()), mWriteNewLine(writeNewLine)
{
}

void ConsoleProgressBar::add(size_t n)
{
    value += n;
    if (autoOutput)
        show();
}

void ConsoleProgressBar::inc()
{
    add(1);
}

std::ostream& ConsoleProgressBar::put(std::ostream& os) const {

    size_t currentBarWidth = 0;
    if (max != 0) currentBarWidth = value * barWidth / max;
    if (currentBarWidth > barWidth) currentBarWidth = barWidth;

    m_time_t elapsed = m_time() - start;
    size_t totalSec = size_t((double)elapsed / ((double)value / (double)max));
    m_time_t etta = totalSec - elapsed;

    char ettaStr[1024];
    tm tm;
    m_gmtime(etta, &tm);
    strftime(ettaStr, sizeof ettaStr, "%H:%M:%S", &tm);
    os << prefix << value << '/' << max << " ETTA " << ettaStr << " [" << string(currentBarWidth, '>') << string(barWidth - currentBarWidth, ' ') << ']';

    return os;
}

