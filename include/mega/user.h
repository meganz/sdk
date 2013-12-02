/*

MEGA SDK - Client Access Engine Core Logic

(c) 2013 by Mega Limited, Wellsford, New Zealand

Applications using the MEGA API must present a valid application key
and comply with the the rules set forth in the Terms of Service.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
DEALINGS IN THE SOFTWARE.

*/

#ifndef MEGA_USER_H
#define MEGA_USER_H 1

#include "attrmap.h"


// user/contact
struct User : public Cachable
{
	// user handle
	handle userhandle;

	// string identifier for API requests (either e-mail address or ASCII user handle)
	string uid;

	// e-mail address
	string email;

	// persistent attributes (n = name, a = avatar)
	AttrMap attrs;

	// visibility status
	visibility show;

	// shares by this user
	handle_set sharing;

	// contact establishment timestamp
	time_t ctime;

	// user's public key
	AsymmCipher pubk;
	int pubkrequested;

	// actions to take after arrival of the public key
	deque<class PubKeyAction*> pkrs;

	void set(visibility, time_t);

	bool serialize(string*);
	static User* unserialize(class MegaClient*, string*);

	User(const char* = NULL);
};


#endif
