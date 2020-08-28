#pragma once
#include <string>
#include <thread>
#include <vector>
extern std::string USER_AGENT;
extern bool gRunningInCI;
extern bool gTestingInvalidArgs;
extern bool gResumeSessions;
extern bool gOutputToCout;
std::ostream& out();
enum { THREADS_PER_MEGACLIENT = 3 };


class TestFS
{
public:
    // these getters should return std::filesystem::path type, when C++17 will become mandatory
    static const std::string& GetTestFolder();
    static const std::string& GetTrashFolder();

    void DeleteTestFolder() { DeleteFolder(GetTestFolder()); }
    void DeleteTrashFolder() { DeleteFolder(GetTrashFolder()); }

    ~TestFS();

private:
    void DeleteFolder(const std::string& folder);

    std::vector<std::thread> m_cleaners;
};
