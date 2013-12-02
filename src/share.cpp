/*

MEGA SDK - Client Access Engine Core Logic

(c) 2013 by Mega Limited, Wellsford, New Zealand

Author: mo
Bugfixing: js, mr

Applications using the MEGA API must present a valid application key
and comply with the the rules set forth in the Terms of Service.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
DEALINGS IN THE SOFTWARE.

*/

#include "mega/share.h"

namespace mega {

Share::Share(User* u, accesslevel a, time_t t)
{
	user = u;
	access = a;
	ts = t;
}

void Share::serialize(string* d)
{
	handle uh = user ? user->userhandle : 0;
	char a = (char)access;

	d->append((char*)&uh,sizeof uh);
	d->append((char*)&ts,sizeof ts);
	d->append((char*)&a,1);
	d->append("",1);
}

bool Share::unserialize(MegaClient* client, int direction, handle h, const byte* key, const char** ptr, const char* end)
{
	if (*ptr+sizeof(handle)+sizeof(time_t)+2 > end) return 0;

	client->newshares.push_back(new NewShare(h,direction,*(handle*)*ptr,(accesslevel)(*ptr)[sizeof(handle)+sizeof(time_t)],*(time_t*)(*ptr+sizeof(handle)),key));

	*ptr += sizeof(handle)+sizeof(time_t)+2;

	return true;
}

void Share::update(accesslevel a, time_t t)
{
	access = a;
	ts = t;
}

// coutgoing: < 0 - don't authenticate, > 0 - authenticate using handle auth
NewShare::NewShare(handle ch, int coutgoing, handle cpeer, accesslevel caccess, time_t cts, const byte* ckey, const byte* cauth)
{
	h = ch;
	outgoing = coutgoing;
	peer = cpeer;
	access = caccess;
	ts = cts;

	if (ckey)
	{
		memcpy(key,ckey,sizeof key);
		have_key = 1;
	}
	else have_key = 0;

	if (outgoing > 0 && cauth)
	{
		memcpy(auth,cauth,sizeof auth);
		have_auth = 1;
	}
	else have_auth = 0;
}

} // namespace
