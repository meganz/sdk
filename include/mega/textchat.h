/**
* @file mega/textchat.h
* @brief Class for manipulating text chat
*
* (c) 2013-2022 by Mega Limited, Wellsford, New Zealand
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

#ifndef TEXT_CHAT_H
#define TEXT_CHAT_H 1

#include "types.h"

#include <vector>
#include <map>

namespace mega {

using std::map;
using std::vector;

#ifdef ENABLE_CHAT
typedef enum { PRIV_UNKNOWN = -2, PRIV_RM = -1, PRIV_RO = 0, PRIV_STANDARD = 2, PRIV_MODERATOR = 3 } privilege_t;
typedef pair<handle, privilege_t> userpriv_pair;
typedef vector< userpriv_pair > userpriv_vector;
typedef map <handle, set <handle> > attachments_map;

struct TextChat : public Cacheable
{
    enum {
        FLAG_OFFSET_ARCHIVE = 0
    };

    handle id;
    privilege_t priv;
    int shard;
    userpriv_vector *userpriv;
    bool group;
    string title;        // byte array
    string unifiedKey;   // byte array
    handle ou;
    m_time_t ts;     // creation time
    attachments_map attachedNodes;
    bool publicchat;  // whether the chat is public or private
    bool meeting;     // chat is meeting room
    byte chatOptions; // each chat option is represented in 1 bit (check ChatOptions struct at types.h)

private:        // use setter to modify these members
    byte flags;     // currently only used for "archive" flag at first bit

public:
    int tag;    // source tag, to identify own changes

    TextChat();
    ~TextChat();

    bool serialize(string *d);
    static TextChat* unserialize(class MegaClient *client, string *d);

    void setTag(int tag);
    int getTag();
    void resetTag();

    struct
    {
        bool attachments : 1;
        bool flags : 1;
        bool mode : 1;
        bool options : 1;
    } changed;

    // return false if failed
    bool setNodeUserAccess(handle h, handle uh, bool revoke = false);
    bool addOrUpdateChatOptions(int speakRequest = -1, int waitingRoom = -1, int openInvite = -1);
    bool setFlag(bool value, uint8_t offset = 0xFF);
    bool setFlags(byte newFlags);
    bool isFlagSet(uint8_t offset) const;
    bool setMode(bool publicchat);

};

typedef vector<TextChat*> textchat_vector;
typedef map<handle, TextChat*> textchat_map;
#endif

} //namespace

#endif
