#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "mega/process.h"

namespace mega
{

class ProcessWithInterceptedOutput
{
public:
    virtual ~ProcessWithInterceptedOutput() = default;
    bool run(const std::vector<std::string>& args, const std::unordered_map<std::string, std::string>& env);
    bool finishedRunning(); // false when not started or still running
    int getExitCode(); // 0 for success

protected:
    virtual void clearBeforeRun() {} // override for member cleanup
    virtual void onOutLine(std::string&& line);
    virtual void onErrLine(std::string&& line);
    virtual void onExit() {}

private:
    void intercept(const unsigned char* data, size_t len, std::string& buffer, std::function<void(std::string&&)> onLine);

    std::unique_ptr<Process> mProc;
    std::string mOutBuffer;
    std::string mErrBuffer;
    bool mExitReported = false;
};

std::string getCurrentTimestamp(bool includeDate = false);

} // namespace mega
