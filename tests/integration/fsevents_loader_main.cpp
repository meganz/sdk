// Loader program to run SDK tests with an open file descriptor to /dev/fsevents
// Command-line arguments to this program will be forwarded to test_integration,
// along with the option --FSEVENTSFD:xxx which passes the file descriptor.
// Must be run with sudo -E or else the environment vars needed for the
// integration tests will not be present

#include <iostream>
#include <string>
#include <vector>

#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

std::string test_integration_binary = "./test_integration";

int main(int argc, char** argv)
{
    int fd = open("/dev/fsevents", O_RDONLY);
    if (fd == -1) {
        std::cerr << "Loader failed to get fsevents fd, error: "
        << strerror(errno) << "\n";
        return 1;
    }

    seteuid(getuid());

    std::vector<char*> myArgv;
    myArgv.push_back(&test_integration_binary[0]);

    for (int i = 1; i < argc; ++i) {
        myArgv.push_back(argv[i]);
    }

    std::string fseventsFdArg = "--FSEVENTSFD:" + std::to_string(fd);
    myArgv.push_back(&fseventsFdArg[0]);
    myArgv.push_back(nullptr);

    execv(&test_integration_binary[0], myArgv.data());
    return 0;
}