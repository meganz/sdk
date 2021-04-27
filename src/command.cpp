/**
 * @file command.cpp
 * @brief Request command component
 *
 * (c) 2013-2014 by Mega Limited, Auckland, New Zealand
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

#include "mega/command.h"
#include "mega/base64.h"
#include "mega/megaclient.h"

namespace mega {
Command::Command()
{
    persistent = false;
    canceled = false;
    result = API_OK;
    client = NULL;
    tag = 0;
    batchSeparately = false;
    suppressSID = false;
}

Command::~Command()
{
}

void Command::cancel()
{
    canceled = true;
}

// returns completed command JSON string
const char* Command::getstring()
{
    return jsonWriter.getstring().c_str();
}

//return true when the response is an error, false otherwise (in that case it doesn't consume JSON chars)
bool Command::checkError(Error& errorDetails, JSON& json)
{
    error e;
    bool errorDetected = false;
    if (json.isNumericError(e))
    {
        // isNumericError already moved the pointer past the integer (name could imply this?)
        errorDetails.setErrorCode(e);
        errorDetected = true;
    }
    else
    {
        const char* ptr = json.pos;
        if (*ptr == ',')
        {
            ptr++;
        }

        if (strncmp(ptr, "{\"err\":", 7) == 0)
        {
            bool exit = false;
            json.enterobject();
            while (!exit)
            {
                switch (json.getnameid())
                {
                    case MAKENAMEID3('e', 'r', 'r'):
                        errorDetails.setErrorCode(static_cast<error>(json.getint()));
                        errorDetected = true;
                        break;
                    case 'u':
                        errorDetails.setUserStatus(json.getint());
                        break;
                    case 'l':
                        errorDetails.setLinkStatus(json.getint());
                        break;
                    case EOO:
                        exit = true;
                        break;
                    default:
                        json.storeobject();
                        break;
                }
            }
            json.leaveobject();
        }
    }

    // generic handling of errors for all commands below

    if (errorDetected && errorDetails == API_EPAYWALL)
    {
        client->activateoverquota(0, true);
    }

#ifdef ENABLE_SYNC
    if (errorDetected && errorDetails == API_EBUSINESSPASTDUE)
    {
        client->syncs.disableSyncs(BUSINESS_EXPIRED, false);
    }
#endif
    return errorDetected;
}

// add opcode
void Command::cmd(const char* cmd)
{
    jsonWriter.cmd(cmd);
}

void Command::notself(MegaClient *client)
{
    jsonWriter.notself(client);
}

// add comma separator unless first element
void Command::addcomma()
{
    jsonWriter.addcomma();
}

// add command argument name:value pair (FIXME: add proper JSON escaping)
void Command::arg(const char* name, const char* value, int quotes)
{
    jsonWriter.arg(name, value, quotes);
}

// binary data
void Command::arg(const char* name, const byte* value, int len)
{
    jsonWriter.arg(name, value, len);
}

void Command::arg(const char* name, NodeHandle h)
{
    jsonWriter.arg(name, h);
}

// 64-bit signed integer
void Command::arg(const char* name, m_off_t n)
{
    jsonWriter.arg(name, n);
}

// raw JSON data
void Command::appendraw(const char* s)
{
    jsonWriter.appendraw(s);
}

// raw JSON data with length specifier
void Command::appendraw(const char* s, int len)
{
    jsonWriter.appendraw(s, len);
}

// begin array
void Command::beginarray()
{
    jsonWriter.beginarray();
}

// begin array member
void Command::beginarray(const char* name)
{
    jsonWriter.beginarray(name);
}

// close array
void Command::endarray()
{
    jsonWriter.endarray();
}

// begin JSON object
void Command::beginobject()
{
    jsonWriter.beginobject();
}

void Command::beginobject(const char *name)
{
    jsonWriter.beginobject(name);
}

// end JSON object
void Command::endobject()
{
    jsonWriter.endobject();
}

// add integer
void Command::element(int n)
{
    jsonWriter.element(n);
}

// add handle (with size specifier)
void Command::element(handle h, int len)
{
    jsonWriter.element(h, len);
}

// add binary data
void Command::element(const byte* data, int len)
{
    jsonWriter.element(data, len);
}

void Command::element(const char *buf)
{
    jsonWriter.element(buf);
}

// open object
void Command::openobject()
{
    jsonWriter.openobject();
}

// close object
void Command::closeobject()
{
    jsonWriter.closeobject();
}

} // namespace

