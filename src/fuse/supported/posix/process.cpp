#include <sys/wait.h>

#include <unistd.h>

#include <cassert>

#include <mega/fuse/common/logging.h>
#include <mega/fuse/platform/process.h>
#include <mega/fuse/platform/utility.h>

namespace mega
{
namespace fuse
{
namespace platform
{

// Close descriptors in the range [begin, SC_OPEN_MAX).
static void closeFrom(int begin);

Process::Process()
  : mInput()
  , mOutput()
  , mID(-1)
{
}

Process::Process(ProcessCallback callback)
  : mInput()
  , mOutput()
  , mID(-1)
{
    FileDescriptor fromChild;
    FileDescriptor fromParent;
    FileDescriptor toChild;
    FileDescriptor toParent;

    // So we can read data from the process.
    std::tie(fromChild, toParent) = pipe(true, false);

    // So we can write data to the process.
    std::tie(fromParent, toChild) = pipe(false, true);

    // Fork a new process.
    auto id = fork();

    // Couldn't fork a new process.
    if (id < 0)
        throw FUSEErrorF("Unable to fork process: %s",
                         std::strerror(errno));

    // We're in the child process.
    if (!id)
    {
        // Execute user callback.
        callback(std::move(fromParent), std::move(toParent));

        // We should never get here.
        exit(0);
    }

    // We're in the parent.
    mID     = id;
    mInput  = std::move(toChild);
    mOutput = std::move(fromChild);
}

Process::Process(Process&& other)
  : mInput(std::move(other.mInput))
  , mOutput(std::move(other.mOutput))
  , mID(other.mID)
{
    other.mID = -1;
}

Process::~Process()
{
    // We have no child.
    if (mID < 0)
        return;

    // Abort the child.
    kill(static_cast<pid_t>(mID), SIGKILL);

    // Wait for the child to terminate.
    wait();
}

Process::operator bool() const
{
    return mID >= 0;
}

bool Process::operator!() const
{
    return mID < 0;
}

Process& Process::operator=(Process&& rhs)
{
    Process temp(std::move(rhs));

    swap(temp);

    return *this;
}

FileDescriptor& Process::input()
{
    return mInput;
}

FileDescriptor& Process::output()
{
    return mOutput;
}

std::size_t Process::read(void* buffer, std::size_t length)
{
    return mOutput.read(buffer, length);
}

void Process::swap(Process& other)
{
    using std::swap;

    swap(mInput, other.mInput);
    swap(mOutput, other.mOutput);
    swap(mID, other.mID);
}

int Process::wait()
{
    for (auto status = 0; ; )
    {
        // Wait for the child to terminate.
        auto id = waitpid(static_cast<pid_t>(mID), &status, 0);

        // Child's terminated.
        if (id >= 0)
        {
            // We no longer have any child.
            mID = -1;

            // Clean up resources.
            Process().swap(*this);

            // Child wasn't aborted.
            if (WIFEXITED(status))
                return WEXITSTATUS(status);

            // Child was aborted.
            return -1;
        }

        // System call was interrupted.
        if (errno == EINTR)
            continue;

        // Couldn't wait for child process.
        throw FUSEErrorF("Couldn't wait for child process: %s",
                         std::strerror(errno));
    }
}

std::size_t Process::write(const void* buffer, std::size_t length)
{
    return mInput.write(buffer, length);
}

Process run(const std::string& command,
            const std::vector<std::string>& arguments)
{
    // Executes command in the child process.
    static const auto execute = [](std::string& command,
                                   std::vector<std::string>& arguments) {
        // Instantiate argument vector.
        std::vector<char*> argv;

        // Reserve space (add 2 for the commmand path and terminator.)
        argv.reserve(arguments.size() + 2);

        // Add command path.
        argv.push_back(&command[0]);

        // Add arguments.
        for (auto& argument : arguments)
            argv.push_back(&argument[0]);

        // Add terminator.
        argv.push_back(nullptr);

        // Try and execute command.
        auto result = execvp(command.c_str(), argv.data());

        // Sanity.
        assert(result < 0);

        // Silence compiler.
        static_cast<void>(result);

        // Couldn't execute command.
        exit(errno);
    }; // execute

    // Execute command in a standard environment.
    auto wrapper = withRedirects(std::bind(execute, command, arguments));

    // Return proess to caller.
    return Process(std::move(wrapper));
}

void swap(Process& lhs, Process& rhs)
{
    lhs.swap(rhs);
}

ProcessCallback withRedirects(std::function<void()> callback)
{
    // Set's up standard redirects.
    static const auto wrapper = [](std::function<void()>& callback,
                                   FileDescriptor fromParent,
                                   FileDescriptor toParent) {
        FileDescriptor stderr(STDERR_FILENO, false);
        FileDescriptor  stdin(STDIN_FILENO,  false);
        FileDescriptor stdout(STDOUT_FILENO, false);

        // Send stderr and stdout to our parent.
        toParent.redirect(stderr);
        toParent.redirect(stdout);
        toParent.reset();

        // stdin reads from our parent.
        fromParent.redirect(stdin);
        fromParent.reset();

        // Close unneeded descriptors.
        closeFrom(STDERR_FILENO + 1);

        // Execute user callback.
        callback();
    }; // wrapper

    // Return wrapper to caller.
    return std::bind(wrapper,
                     std::move(callback),
                     std::placeholders::_1,
                     std::placeholders::_2);
}

void closeFrom(int current)
{
    // Sanity.
    assert(current >= 0);

    // What is the maximum number of open descriptors?
    auto max = sysconf(_SC_OPEN_MAX);

    // Close descriptors.
    for ( ; current < max; ++current)
    {
        // Close the descriptor.
        while (close(current) < 0 && errno == EINTR)
            ;
    }
}

} // platform
} // fuse
} // mega

