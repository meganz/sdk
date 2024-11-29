/**
 * Covenience, include this file instead of platform headers
 *
 * */

#pragma once

#if defined(WIN32)
#include "mega/win32/gfx/worker/comms_client.h"
#else
#include "mega/posix/gfx/worker/comms_client.h"
#endif
