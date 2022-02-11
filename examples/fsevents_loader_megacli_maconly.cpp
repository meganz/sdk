// Loader program to run megacli with an open file descriptor to /dev/fsevents
// passed with the option --FSEVENTSFD:xxx which passes the file descriptor.
// Without this, on Mac, the filesystem notifications of changes are not delivered properly.

// Once this executable is built, give it (but not megacli) root permissions
// so that it can get filesystem notifications with these commands:
// sudo chown root ./megacli_fsloader
// sudo chmod +s ./megacli_fsloader 

#include <iostream>
#include <string>
#include <vector>

#include <errno.h>
#include <fcntl.h>
#include <unistd.h>


int main(int argc, char** argv)
{
    int fd = open("/dev/fsevents", O_RDONLY);
    if (fd == -1) {
        std::cerr << "Loader failed to get fsevents fd, error: "
        << strerror(errno) << "\n";
        return 1;
    }

    seteuid(getuid());

    std::string test_integration_binary = "./megacli";
    if (argc > 1) test_integration_binary = argv[1];

    std::vector<const char*> myArgv;
    myArgv.push_back(test_integration_binary.c_str());

    for (int i = 2; i < argc; ++i) {
        myArgv.push_back(argv[i]);
    }

    std::string fseventsFdArg = "--FSEVENTSFD:" + std::to_string(fd);
    myArgv.push_back(&fseventsFdArg[0]);
    myArgv.push_back(nullptr);

    execv(test_integration_binary.c_str(), (char**)myArgv.data());
    return 0;
}
