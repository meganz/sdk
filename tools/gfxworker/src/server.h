/**
 * Covenience, include this file instead of platform headers
 *
 * */

#pragma once

#if defined(WIN32)
#include "win32/server.h"
#else
#include "posix/server.h"
#endif


namespace mega {
namespace gfx {

#if defined(WIN32)
using Server = ServerWin32;
#else
using Server = ServerPosix;
#endif

}
}
