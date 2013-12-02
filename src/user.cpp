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

#include "mega/user.h"
#include "mega/megaclient.h"

namespace mega {

User::User(const char* cemail)
{
	userhandle = UNDEF;
	show = VISIBILITY_UNKNOWN;
	ctime = 0;
	pubkrequested = 0;
	if (cemail) email = cemail;
}

bool User::serialize(string* d)
{
	unsigned char l;
	attr_map::iterator it;

	d->reserve(d->size()+100+attrs.storagesize(10));

	d->append((char*)&userhandle,sizeof userhandle);
	d->append((char*)&ctime,sizeof ctime);
	d->append((char*)&show,sizeof show);

	l = email.size();
	d->append((char*)&l,sizeof l);
	d->append(email.c_str(),l);

	d->append("\0\0\0\0\0\0\0",8);

	attrs.serialize(d);

	if (pubk.isvalid()) pubk.serializekey(d,AsymmCipher::PUBKEY);

	return true;
}

User* User::unserialize(MegaClient* client, string* d)
{
	handle uh;
	time_t ts;
	visibility v;
	unsigned char l;
	string m;
	User* u;
	const char* ptr = d->data();
	const char* end = ptr+d->size();
	int i;

	if (ptr+sizeof(handle)+sizeof(time_t)+sizeof(visibility)+2 > end) return NULL;

	uh = *(handle*)ptr;
	ptr += sizeof uh;

	ts = *(time_t*)ptr;
	ptr += sizeof ts;

	v = *(visibility*)ptr;
	ptr += sizeof v;

	l = *ptr++;
	if (l) m.assign(ptr,l);
	ptr += l;

	for (i = 8; i--; ) if (ptr+*(unsigned char*)ptr < end) ptr += *(unsigned char*)ptr+1;

	if (i >= 0 || !(u = client->finduser(uh,1))) return NULL;

	if (v == ME) client->me = uh;
	client->mapuser(uh,m.c_str());
	u->set(v,ts);

	if (ptr < end && !(ptr = u->attrs.unserialize(ptr,end-ptr))) return NULL;

	if (ptr < end && !u->pubk.setkey(AsymmCipher::PUBKEY,(byte*)ptr,end-ptr)) return NULL;

	return u;
}

// update user attributes
void User::set(visibility v, time_t ct)
{
	show = v;
	ctime = ct;
}

} // namespace
