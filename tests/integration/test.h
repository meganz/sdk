#pragma once
#include <string>
extern std::string USER_AGENT;
extern bool gRunningInCI;
extern bool gTestingInvalidArgs;
extern bool gResumeSessions;
enum { THREADS_PER_MEGACLIENT = 3 };
