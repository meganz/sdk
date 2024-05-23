#pragma once

#define LINUX_ONLY(l)
#define LINUX_OR_POSIX(l, p) p
#define POSIX_ONLY(p) p
#define UNIX_ONLY(u) u
#define UNIX_OR_WINDOWS(u, w) u
#define WINDOWS_ONLY(w)

