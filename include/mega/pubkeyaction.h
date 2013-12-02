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

#ifndef MEGA_PUBKEYACTION_H
#define MEGA_PUBKEYACTION_H 1

#include "mega/megaclient.h"
#include "mega/user.h"
#include "mega/node.h"

namespace mega {

// action to be performed upon arrival of a user's public key
class PubKeyAction
{
public:
	int tag;

	virtual void proc(MegaClient*, User*) = 0;

	virtual ~PubKeyAction() { }
};

class PubKeyActionCreateShare : public PubKeyAction
{
	handle h;	// node to create share on
	accesslevel a;	// desired access level

public:
	void proc(MegaClient*, User*);

	PubKeyActionCreateShare(handle, accesslevel, int);
};

class PubKeyActionSendShareKey : public PubKeyAction
{
	handle sh;	// share node the key was requested on

public:
	void proc(MegaClient*, User*);

	PubKeyActionSendShareKey(handle);
};

class PubKeyActionPutNodes : public PubKeyAction
{
	NewNode* nn;	// nodes to add
	int nc;			// number of nodes to add

public:
	void proc(MegaClient*, User*);

	PubKeyActionPutNodes(NewNode*, int, int);
};

} // namespace

#endif
