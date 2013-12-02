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

#include "mega/pubkeyaction.h"
#include "mega/megaapp.h"
#include "mega/command.h"

PubKeyActionPutNodes::PubKeyActionPutNodes(NewNode* newnodes, int numnodes, int ctag)
{
	nn = newnodes;
	nc = numnodes;
	tag = ctag;
}

void PubKeyActionPutNodes::proc(MegaClient* client, User* u)
{
	if (u)
	{
		byte buf[AsymmCipher::MAXKEYLENGTH];
		int t;

		// re-encrypt all node keys to the user's public key
		for (int i = nc; i--; )
		{
			if (!(t = u->pubk.encrypt((const byte*)nn[i].nodekey.data(),nn[i].nodekey.size(),buf,sizeof buf))) return client->app->putnodes_result(API_EINTERNAL,USER_HANDLE,nn);

			nn[i].nodekey.assign((char*)buf,t);
		}

		client->reqs[client->r].add(new CommandPutNodes(client,UNDEF,u->uid.c_str(),nn,nc,tag));
	}
	else client->app->putnodes_result(API_ENOENT,USER_HANDLE,nn);
}

// sharekey distribution request for handle h
PubKeyActionSendShareKey::PubKeyActionSendShareKey(handle h)
{
	sh = h;
}

void PubKeyActionSendShareKey::proc(MegaClient* client, User* u)
{
	Node* n;

	// only the share owner distributes share keys
	if (u && (n = client->nodebyhandle(sh)) && n->sharekey && client->checkaccess(n,OWNER))
	{
		int t;
		byte buf[AsymmCipher::MAXKEYLENGTH];

		if ((t = u->pubk.encrypt(n->sharekey->key,SymmCipher::KEYLENGTH,buf,sizeof buf))) client->reqs[client->r].add(new CommandShareKeyUpdate(client,sh,u->uid.c_str(),buf,t));
	}
}

void PubKeyActionCreateShare::proc(MegaClient* client, User* u)
{
	Node* n;
	int newshare;

	// node vanished: bail
	if (!(n = client->nodebyhandle(h))) return client->app->share_result(API_ENOENT);

	// do we already have a share key for this node?
	if ((newshare = !n->sharekey))
	{
		// no: create
		byte key[SymmCipher::KEYLENGTH];

		PrnGen::genblock(key,sizeof key);

		n->sharekey = new SymmCipher(key);
	}

	// we have all ingredients ready: the target user's public key, the share key and all nodes to share
	client->restag = tag;
	client->reqs[client->r].add(new CommandSetShare(client,n,u,a,newshare));
}

// share node sh with access level sa
PubKeyActionCreateShare::PubKeyActionCreateShare(handle sh, accesslevel sa, int ctag)
{
	h = sh;
	a = sa;
	tag = ctag;
}

