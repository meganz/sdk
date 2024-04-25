#include <functional>
#include <string>
#include <vector>

#include <mega/fuse/platform/file_descriptor.h>
#include <mega/fuse/platform/process_forward.h>

namespace mega
{
namespace fuse
{
namespace platform
{

// Convenience.
using ProcessCallback =
    std::function<void(FileDescriptor input,
                       FileDescriptor output)>;

class Process
{
    // A descriptor representing this process's standard input.
    FileDescriptor mInput;

    // A descriptor representing this process's standard output.
    FileDescriptor mOutput;

    // The process's ID.
    long mID;

public:
    Process();

    // Instantiate a new process that will execute callback.
    explicit Process(ProcessCallback callback);

    // Take ownership of an existing process.
    Process(Process&& other);

    // Destroy process, aborting child if necessary.
    ~Process();

    // True if this instance represents a process.
    operator bool() const;

    // True if this instance doesn't represent a process.
    bool operator!() const;

    // Move one process to another.
    Process& operator=(Process&& rhs);

    // Retrieve a descriptor you can use to read data from the child.
    FileDescriptor& input();

    // Retrive a descriptor you can use to send data to the child.
    FileDescriptor& output();

    // Read some data from the process.
    std::size_t read(void* buffer, std::size_t length);

    // Swap this process with another.
    void swap(Process& other);
    
    // Wait for the child to terminate.
    int wait();

    // Write some data to the process.
    std::size_t write(const void* buffer, std::size_t length);
}; // Process

// Run the specified command as a new process.
Process run(const std::string& command,
            const std::vector<std::string>& arguments);

// Swap lhs with rhs.
void swap(Process& lhs, Process& rhs);

// Executes callback in an environment where:
// - stderr and stdout can be read on the parent's input.
// - The parent's output can be read on the child's stdin.
ProcessCallback withRedirects(std::function<void()> callback);

// Convenience.
template<typename Callback>
ProcessCallback withRedirects(Callback&& callback)
{
    // Adapt callback to needed interface.
    std::function<void()> wrapper(std::forward<Callback>(callback));

    // Wrap callback.
    return withRedirects(std::move(wrapper));
}

} // platform
} // fuse
} // mega

