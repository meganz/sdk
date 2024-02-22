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
    canceled = false;
    result = API_OK;
    client = NULL;
    tag = 0;
    batchSeparately = false;
}

Command::~Command()
{
}

void Command::cancel()
{
    canceled = true;
}

void Command::addToNodePendingCommands(Node* node)
{
    node->mPendingChanges.push_back(this);
}

void Command::removeFromNodePendingCommands(NodeHandle h, MegaClient* client)
{
    if (auto node = client->nodeByHandle(h))
    {
        node->mPendingChanges.erase(this);
    }
}

// returns completed command JSON string
const char* Command::getJSON(MegaClient*)
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

    if (errorDetected && errorDetails == API_EBUSINESSPASTDUE)
    {
        client->setBusinessStatus(BIZ_STATUS_EXPIRED);
    }

    return errorDetected;
}

#ifdef ENABLE_CHAT
void Command::createSchedMeetingJson(const ScheduledMeeting* schedMeeting)
{
    if (!schedMeeting)
    {
        assert(false);
        return;
    }

    // note: we need to B64 encode the following params: timezone(tz), title(t), description(d), attributes(at)
    handle chatid = schedMeeting->chatid();
    handle schedId = schedMeeting->schedId();
    handle parentSchedId = schedMeeting->parentSchedId();

    // chatid is only required when we create a scheduled meeting for an existing chat
    if (!ISUNDEF(chatid))	{ arg("cid", (byte*)& chatid, MegaClient::CHATHANDLE); }

    // required params
    arg("tz", Base64::btoa(schedMeeting->timezone()).c_str());
    arg("s", schedMeeting->startDateTime());
    arg("e", schedMeeting->endDateTime());
    arg("t", Base64::btoa(schedMeeting->title()).c_str());
    arg("d", Base64::btoa(schedMeeting->description()).c_str());

    // optional params
    if (!ISUNDEF(schedId))                          { arg("id", (byte*)&schedId, MegaClient::CHATHANDLE); } // scheduled meeting ID
    if (!ISUNDEF(parentSchedId))                    { arg("p", (byte*)&parentSchedId, MegaClient::CHATHANDLE); } // parent scheduled meeting ID
    if (schedMeeting->cancelled() >= 0)             { arg("c", schedMeeting->cancelled()); }
    if (!schedMeeting->attributes().empty())        { arg("at", Base64::btoa(schedMeeting->attributes()).c_str()); }

    if (schedMeeting->flags() && !schedMeeting->flags()->isEmpty())
    {
        arg("f", static_cast<long>(schedMeeting->flags()->getNumericValue()));
    }

    if (MegaClient::isValidMegaTimeStamp(schedMeeting->overrides()))
    {
        arg("o", schedMeeting->overrides());
    }

    // rules are not mandatory to create a scheduled meeting, but if provided, frequency is required
    if (schedMeeting->rules())
    {
        const ScheduledRules* rules = schedMeeting->rules();
        beginobject("r");

        if (rules->isValidFreq(rules->freq()))
        {
            arg("f", rules->freqToString()); // required
        }

        if (rules->isValidInterval(rules->interval()))
        {
            arg("i", rules->interval());
        }

        if (MegaClient::isValidMegaTimeStamp(rules->until()))
        {
            arg("u", rules->until());
        }

        if (rules->byWeekDay() && !rules->byWeekDay()->empty())
        {
            beginarray("wd");
            for (auto i: *rules->byWeekDay())
            {
                element(static_cast<int>(i));
            }
            endarray();
        }

        if (rules->byMonthDay() && !rules->byMonthDay()->empty())
        {
            beginarray("md");
            for (auto i: *rules->byMonthDay())
            {
                element(static_cast<int>(i));
            }
            endarray();
        }

        if (rules->byMonthWeekDay() && !rules->byMonthWeekDay()->empty())
        {
            beginarray("mwd");
            for (auto i: *rules->byMonthWeekDay())
            {
                beginarray();
                element(static_cast<int>(i.first));
                element(static_cast<int>(i.second));
                endarray();
            }
            endarray();
        }
        endobject();
    }
}
#endif

// cache urls and ips given in response to avoid further waiting for dns resolution
bool Command::cacheresolvedurls(const std::vector<string>& urls, std::vector<string>&& ips)
{
    // cache resolved URLs if received
    return client->httpio->cacheresolvedurls(urls, std::move(ips));
}

// Store ips from response in the vector passed
bool Command::loadIpsFromJson(std::vector<string>& ips, JSON& json)
{
    if (json.enterarray()) // for each URL, there will be 2 IPs (IPv4 first, IPv6 second)
    {
        for (;;)
        {
            std::string ti;
            if (!json.storeobject(&ti))
            {
                break;
            }
            ips.emplace_back(std::move(ti));
        }
        json.leavearray();
        return true;
    }
    return false;
}

// add opcode
void Command::cmd(const char* cmd)
{
    jsonWriter.cmd(cmd);
    commandStr = cmd;
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

