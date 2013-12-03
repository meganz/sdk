/**
 * @file mega.h
 * @brief Main header file for inclusion by client software.
 *
 * (c) 2013 by Mega Limited, Wellsford, New Zealand
 *
 * This file is part of the MEGA SDK - Client Access Engine.
 *
 * Applications using the MEGA API must present a valid application key
 * and comply with the the rules set forth in the Terms of Service.
 *
 * The MEGA SDK is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * @copyright Simplified (2-clause) BSD License.
 *
 * You should have received a copy of the license along with this
 * program.
 */

#ifndef MEGA_H
#define MEGA_H 1

#ifndef MEGA_SDK
#define MEGA_SDK
#endif

// project types
#include "types.h"

// project includes
#include "account.h"
#include "http.h"
#include "attrmap.h"
#include "backofftimer.h"
#include "base64.h"
#include "command.h"
#include "console.h"
#include "fileattributefetch.h"
#include "filefingerprint.h"
#include "file.h"
#include "filesystem.h"
#include "db.h"
#include "json.h"
#include "pubkeyaction.h"
#include "request.h"
#include "serialize64.h"
#include "share.h"
#include "sharenodekeys.h"
#include "treeproc.h"
#include "user.h"
#include "utils.h"
#include "waiter.h"

#include "node.h"
#include "sync.h"
#include "transfer.h"
#include "transferslot.h"
#include "commands.h"
#include "megaapp.h"
#include "megaclient.h"

// Windows specific includes
#ifdef _WIN32
#include "win32/net.h"
#include "win32/fs.h"
#include "win32/console.h"
#include "win32/net.h"

// Linux specific includes
#else
#include "posix/net.h"
#include "posix/fs.h"
#include "posix/console.h"
#include "posix/net.h"
#endif

#include "db/sqlite.h"

#endif
