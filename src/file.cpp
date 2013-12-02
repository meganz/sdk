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

#include "mega/file.h"
#include "mega/transfer.h"
#include "mega/transferslot.h"
#include "mega/megaclient.h"
#include "mega/sync.h"
#include "mega/command.h"

File::File()
{
	transfer = NULL;
}

File::~File()
{
	// if transfer currently running, stop
	if (transfer) transfer->client->stopxfer(this);
}

void File::prepare()
{
	transfer->localfilename = localname;
}

void File::start()
{
}

void File::progress()
{
}

void File::completed(Transfer* t, LocalNode* l)
{
	if (t->type == PUT)
	{
		NewNode* newnode = new NewNode[1];

		// build new node
		newnode->source = NEW_UPLOAD;

		// upload handle required to retrieve/include pending file attributes
		newnode->uploadhandle = t->uploadhandle;

		// reference to uploaded file
		memcpy(newnode->uploadtoken,t->slot->ultoken,sizeof newnode->uploadtoken);

		// file's crypto key
		newnode->nodekey.assign((char*)t->filekey,Node::FILENODEKEYLENGTH);
		newnode->clienttimestamp = t->mtime;
		newnode->type = FILENODE;
		newnode->parenthandle = UNDEF;

		if ((newnode->localnode = l)) newnode->syncid = l->syncid;

		AttrMap attrs;

		// store filename
		attrs.map['n'] = name;

		// store fingerprint
		t->serializefingerprint(&attrs.map['c']);

		string tattrstring;

		attrs.getjson(&tattrstring);

		t->client->makeattr(&t->key,&newnode->attrstring,tattrstring.c_str());

		if (targetuser.size())
		{
			// drop file into targetuser's inbox
			t->client->putnodes(targetuser.c_str(),newnode,1);
		}
		else
		{
			handle th = h;

			// inaccessible target folder - use / instead
			if (!t->client->nodebyhandle(th)) th = t->client->rootnodes[0];

			if (l) t->client->syncadding++;
			t->client->reqs[t->client->r].add(new CommandPutNodes(t->client,th,NULL,newnode,1,l ? l->sync->tag : 0,l ? PUTNODES_SYNC : PUTNODES_APP));
		}
	}
}

// do not retry crypto errors or administrative takedowns; retry other types of failuresup to 16 times
bool File::failed(error e)
{
	return e != API_EKEY && e != API_EBLOCKED && transfer->failcount < 16;
}

void File::displayname(string* dname)
{
	if (name.size()) *dname = name;
	else
	{
		Node* n;

		if ((n = transfer->client->nodebyhandle(h))) *dname = n->displayname();
		else *dname = "DELETED/UNAVAILABLE";
	}
}

SyncFileGet::SyncFileGet(Node* cn, string* clocalname)
{
	n = cn;
	h = n->nodehandle;
	*(FileFingerprint*)this = *n;
	localname = *clocalname;

	n->syncget = this;
}

SyncFileGet::~SyncFileGet()
{
	n->syncget = NULL;
}

// complete, then self-destruct
void SyncFileGet::completed(Transfer* t, LocalNode* n)
{
	File::completed(t,n);
	delete this;
}
