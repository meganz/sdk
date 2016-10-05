/**
 * @file examples/megacmd/megacmd.h
 * @brief MegaCMD: Interactive CLI and service application
 *
 * (c) 2013-2016 by Mega Limited, Auckland, New Zealand
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

#ifndef MEGACMD_H
#define MEGACMD_H

#include "megaapi.h"

using namespace std;
using namespace mega;

typedef struct sync_struct
{
    MegaHandle handle;
    bool active;
    std::string localpath;
    long long fingerprint;
} sync_struct;


enum prompttype
{
    COMMAND, LOGINPASSWORD, OLDPASSWORD, NEWPASSWORD, PASSWORDCONFIRM
};

static const char* prompts[] =
{
    "MEGA CMD> ", "Password:", "Old Password:", "New Password:", "Retype New Password:"
};

void changeprompt(const char *newprompt);

MegaApi* getFreeApiFolder();
void freeApiFolder(MegaApi *apiFolder);

const char * getUsageStr(const char *command);

void setprompt(prompttype p);

void printHistory();


#endif
